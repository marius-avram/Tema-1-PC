#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "lib.h"
#include "crc.h"

#define CEIL(a, b) ((a) / (b) + ((a) % (b) ? 1 : 0))


#define HOST "127.0.0.1"
#define PORT 10000

typedef struct { 
	int speed; 
	int delay;
	char *filename;
} physics_params; 

//Functia primeste un parametrul al programului send
//(parametru dat in linie de comanda si returneaza valoarea 
//indicata de acesta). 
physics_params get_params(int argn, char** argv){
	int i;	
	char *arg, *type, *value;
	physics_params pp;
	for(i=0; i<argn; i++){ 
		arg = argv[i];
		
		type = strtok(arg, "="); //Primul camp contine tipul paramentrului
		value = strtok(NULL, "="); //Al doilea camp contine valoarea 
		
		if (value==NULL && i!=0){ 
			//Atunci am ajuns la numele fisierului
			pp.filename = strdup(type);
			continue;
		}
		
		if (value!=NULL && type!=NULL){ 
			if (strcmp(type, "speed") == 0){
				pp.speed = atoi(value); 
			}
			else if(strcmp(type, "delay") == 0){
				pp.delay = atoi(value);
			}
		}
	} 
	return pp;
	
}



void send_file(physics_params p, word* crctable){
  msg t;
  int fpcycle; // frames per cycle (cadre pe ciclu) 
  // In functie de viteza si de delayul retelei va trebui 
  // sa trimite un anumit numar de cadre pana sa vina primul ACK
  int waiting_interval, frame_count, SEQ, sent, windowSize;
  int i, buffered;
  int* notACK;
  char readd[1389];
  struct stat buf;
  msg* r = NULL; 
  msg * msg_buffer, * pmsg;
  
  if (stat(p.filename,&buf)<0){
    perror("Stat failed");
    return;
  }

  int fd = open(p.filename, O_RDONLY);

  if (fd<0){
    perror("Couldn't open file");
    return;
  }
  

	if (p.delay == 0) { 
	  	p.delay = 1;
	}
	
	//Calculam parametrii de transimisie(cate cadre se trimit pana
	//la prima confirmare - SEQ).

	fpcycle = CEIL(p.speed * p.delay * 1000, sizeof(msg)*8);
	
	
	if (fpcycle < 10){
		fpcycle = 10;
	}

	waiting_interval = p.delay;
	
  
  
  //printf("frames_per_cycle = %i\n", fpcycle);
  //printf("waiting_interval = %i\n", waiting_interval);
  
  
  
  //Se trimite mai intai numele fisierului, lungimea mesajului si 
  //nr de SEQ (Secvente trimise pana cand vine prima confirmare;
  //In momentul in care a venit confirmarea nr SEQ se va reseta).
  t.type = 1;
  
  sprintf(t.payload,"%s\n%d\n%d\n",p.filename,(int)buf.st_size,fpcycle);
  sprintf(t.payload,"%s%d\n", t.payload, crcForString(t.payload, crctable));
  t.len = strlen(t.payload)+1;
  
  send_message(&t);
  
  r = NULL; 
  
  //Se trimite mesajul de initializare conexiune (care contine informatii 
  //despre dimensiunea fisierului care trebuie transmis si dimensiunea 
  //ferestrei) pana cand se va primi o confirmare. Este nevoie de 
  //acest lucru pentru a fi siguri de faptul ca destinatarul la primit;
  //ele asigura buna functionare a algoritmului din reciever.
  while (1) {
  	r = receive_message_timeout(10); 
	if (!r) {
		printf("$");
	}
  	if (r != NULL) {
  		if (r->type == 3){
  			windowSize = atoi(r->payload);
  			break;
  		}
  		else { 
  			//S-a primit un NAK deci mai trebuie trimis o data
  			send_message(&t);
  		}
  	}
  }
  
  //Acum trebuie sa citim din fisier un numar de cadre egal cu 
  //frames_per_cycle+1 pentru a avea un buffer cu ultimele cadre 
  //in cazul in care se produce vreo eroare
  
  //Tinem cont si de parametrul dat lui reciever care spune ce
  //dimenisiune maxima a ferestrei accepta acesta 
  if (windowSize < fpcycle && windowSize != 0){ 
		fpcycle = windowSize;
  }
  
  
  if (fpcycle < 10){	
  		fpcycle = 10;
  }

  msg_buffer = (msg*)calloc((fpcycle+1),sizeof(msg));
  buffered = 0;
  pmsg = msg_buffer;
  while (buffered <= fpcycle){
  	msg_buffer->type = 2;
  	if ((msg_buffer->len = read(fd, &readd, 1389))<=0){
  		//Inseamna ca s-a ajuns la sfarsitul fisierului,
  		//bufferul e mai mare decat dimensiunea fisierului
  		break;
  	}
  	strcpy(msg_buffer->payload, encode_payload(readd, buffered, crctable));
  	msg_buffer++;
  	buffered++;
  }
  
  
  msg_buffer = pmsg;
 
 
  notACK = (int*)calloc(fpcycle+1,sizeof(int));
  frame_count = -1;
  SEQ = -1;
  sent = buffered;
  
  //Vom trimite mesaje pana nu se mai poate pune nimic in buffer, 
  //adica fisierul a fost citit pana la sfarsit.
  while (1){
	
	frame_count++;
	SEQ++;
	if (SEQ > fpcycle) { 
		SEQ = 0;
	}
	
    r = NULL;  
     
    // Daca s-a depasit estimarea initiala de cadre trimise
    // se asteapta pana vine urmatorul ACK. Pentru ca s-ar putea 
    // sa intarzie si nu vrem sa il ratam inainte de a reseta SEQ
    if (frame_count > fpcycle) {
    	
    	//Am decis sa astept mesajul pana acesta vine. Un mesaj
    	//va veni la un moment dat fie el ACK sau NAK.
		while (r == NULL){
			r = receive_message_timeout(waiting_interval);
		}
		
		if (r->type == 3) { 
				
			// Inseamna ca putem inlocui un mesaj din buffer cu unul nou
			pmsg = msg_buffer+SEQ;
			pmsg->type  = 2; 
			pmsg->len = read(fd, &readd, 1389);
			if (pmsg->len <= 0) { 
				break;
			}
			
			sent++;
			strcpy(pmsg->payload, encode_payload(readd, SEQ, crctable));
			notACK[SEQ] = 0;
		}
		
	
		
    }
    
	//In cazul in care nu a venit un mesaj de tip 3 (ACK) inseamna 
	//ca a ajuns un mesaj de tip NAK => ca trebuie sa mai trimitem 
	//din nou acelasi pachet. Daca e de tip 3 pachetul a fost incarcat 
	//cu unul nou din fisier in ramura if.
	t = *(msg_buffer+SEQ);
	
    send_message(&t);
  
  } 
   
    
  for (i=0; i<=fpcycle; i++){
  	notACK[i] = 1;
  } 
  
  
  // Asteptam si confirmarile pe care nu le-am primit inca
  while(1){ 
  	
  	SEQ++; 
  	if (SEQ > fpcycle){ 
  		SEQ = 0;
  	}
  	
  	r= NULL; 
  	
  	//Din nou se asteapta pana vine un mesaj.
  	while(!r){
		r= receive_message_timeout(waiting_interval); 
	}
	
	
	//Daca e un mesaj ACK atunci acesta trebuie marcat 
	//in notACK pentru ca lungimea campului sa fie modificata 
	//astfel incat destinatarul sa stie ca ar trebui sa nu le
	//mai scrie in fisier. E necesar pentru ca e posibil sa
	//il mai retrimitem de mai multe ori pana cand vor fi confirmate 
	//toate pachetele.
	if (r->type == 3){ 
		notACK[SEQ] = 0;
	}
	
	if (r->type == 6){
		//Inseamna ca s-a primit toate datele la reciever 
		break;		
	}
	

	t = *(msg_buffer+SEQ); 
	if (notACK[SEQ] == 0){ 
		t.len = -1;
	}
	send_message(&t);
  }
  
  //Facem eliberarea de spatiu si inchidem fisierul 
  free(msg_buffer);
  free(notACK);
  msg_buffer = NULL;
  notACK = NULL;
  close(fd);
}

int main(int argc,char** argv){
  init(HOST,PORT);
  //Preluam parametrii din linia de comanda
  physics_params p = get_params(argc, argv);	
  
  if (argc<2){
    printf("Usage %s filename\n",argv[0]);
    return -1;
  }
  
  //Vom genera tabela de coduri CRC, pentru ca avem 
  //o cantitate mare informatie si va fi mai eficient sa 
  //o folosim.
  word* crctable = tabelcrc(256);
  send_file(p, crctable);
  return 0;
}
