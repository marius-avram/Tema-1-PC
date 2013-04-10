#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "lib.h"
#include "crc.h"

#define HOST "127.0.0.1"
#define PORT 10001
#define MAX_BUFFER_SIZE 5

//Va determina parametrii conexiunii stabilite: numele fisierului primit,
//dimensiunea acestuia si numarul de cadre primite pana cand prima 
//confirmarea va ajunge la sender (SEQ).
int connection_info(msg* r, char* filename , int*fs, int*fc, word* crctable){
	char* filesize; 
	char* frame_count;
	char * temp_filename;
	char payload_beg[1400];

	word crcCode = 0, crcRecieved = 0;
	//Facem verificari sa vedem ca am primit pachetul cu 
	//informatii despre conexiune
	if (r->type!=1){
		printf("Expecting filename and size message\n");
		return -1;
	}
	
	if (r->len > 1400){ 
		printf("Malformed message received! Bailing out");
		return -1;
	}
	
   	temp_filename = strtok(r->payload," \n");
   	filesize = strtok(NULL," \n"); 
   	frame_count = strtok(NULL, " \n"); 
   	
   	
	if (temp_filename == NULL || filesize == NULL || frame_count == NULL){ 
		return -1;
	}
	
	
	sprintf(payload_beg, "%s\n%s\n%s\n", temp_filename, filesize, frame_count); 
	crcCode = crcForString(payload_beg, crctable);
	
	crcRecieved = atoi(strtok(NULL, "\n"));
	
	if (crcRecieved != crcCode) {
	
		return -2;
	}
	
   	strcpy(filename, temp_filename);
   	
	*fs = atoi(filesize); 
	*fc = atoi(frame_count);

	return 0;
		
}

//O tentativa de a copia atat caracterele \0 in stringul destinatie
//first.
void mystrncpy(char* first, char* second, int len){
	int i;
	
	for(i=0; i<len; i++){ 
		first[i] = second[i];
	}
}

//Preia valoarea parametrului windows dat in linia de comanda.
//Face niste parasari simple de text.
char* getWindowSize(int argn, char** argv){ 
	char *param, *window, *windowValue;
	int i;
	for (i=0; i<argn; i++){
		param = argv[i];
		window = strtok (param, "=");
		if (strcmp(window,"window") == 0){
			windowValue = strtok(NULL, "=");
		}
	}
	
	return windowValue;
	
}

//Scrie un Subbuffer in fisier
void writeSubBuffer(msg *subBuffer, int frame_count, int fd){
	int i;
	for(i=0; i<frame_count; i++){
	
		if (subBuffer[i].type != 5 && subBuffer[i].len !=-1){
			printf("&");
			printf("buffer len: %i", subBuffer[i].len);
			write(fd, subBuffer[i].payload, subBuffer[i].len);
		}
		
	}
}

//Le marcam cu 5 pe cele inactive
void markInactive(int pos, int frame_count, msg*** buffer){
	int i,j, okSubBuffer;
		
		if (pos > 0){
			for(i=0; i<=frame_count; i++){
				if ((*buffer)[pos-1][i].type == 4 || (*buffer)[pos-1][i].type == 5){
					(*buffer)[pos][i].type = 5;
				}
				okSubBuffer = 1;
				for(j=0; j<pos; j++){ 
					if ((*buffer)[j][i].type == 4) {
						okSubBuffer = 0;
					}
				}
				if (okSubBuffer) { 
					(*buffer)[pos][i].type = 0;
				}	
			}
		}

}

//Realoca Bufferul (in cazul in care dimensiunea este prea mare)
int reallocBuffer(int pos, int frame_count, int *maxBufferSize, msg *** buffer){ 
	msg ** nbuffer;
	int k;
		
	if (pos>=*maxBufferSize){
			//trebuie dublata dimensiunea bufferului
			*maxBufferSize *= 2;
			nbuffer = (msg**)realloc(*buffer, sizeof(msg*)*(*maxBufferSize));
		
			if (!nbuffer){ 
				printf("Out of memory\n");
				return -1;
			}
			else {
				//Realocarea a reusit 
				*buffer = nbuffer;
			}
		
			for(k=*maxBufferSize/2; k<*maxBufferSize; k++){ 
				(*buffer)[k] = (msg*)calloc((frame_count+1),sizeof(msg));
				if ((*buffer)[k] == NULL){ 
					printf("Out of memory\n");
					return -1;
				}
			}
						

	}
	return 0;
}



int main(int argc,char** argv){
	msg *r,t;
	init(HOST,PORT);
	char filename[1400];
	int fs, i, round, depl = 0;
	int frame_count, seq, send_seq, aux_seq1, aux_seq2, okCrc, okSubBuffer, ws;
	word* crctable = tabelcrc(256); //Tabela de coduri CRC
	char payloadContent[1390];
	char *windowSize;
	
	//Variabile pentru buffer
	msg **buffer = NULL;
	int pos = 0, maxBufferSize = MAX_BUFFER_SIZE;
	
	windowSize = getWindowSize(argc, argv);
	
	//Ia parametrii conexiunii. E posibil sa se produca erori 
	//si aici, de aceea vom face un ciclu.
	while (1){
		r = receive_message_timeout(10);
		if (r!=NULL) {
			if (connection_info(r, filename, &fs, &frame_count, crctable) == 0){
			
				break;
			}
		}
	}   
	
	//Aici actualizam dimensiunea ferestrei
	ws = atoi(windowSize);
	if (ws != 0) {
		if (ws < frame_count && ws >=10){
			frame_count = ws;
		}
		else if (ws < frame_count && ws<10){
			frame_count = 10;
		}
	}
	
	//Alocam spatiu in memorie pentru un buffer care va retine cadrele 
	//primite. Scrierea in fisier se va face abia cand avem un numar de 
	//cadre consecutive confirmate egal cu SEQ.
	buffer = (msg**)calloc(maxBufferSize,sizeof(msg*));
	if (buffer == NULL){ 
		printf("Out of memory\n");
		return -1;
	}
	
	for(i=0; i<maxBufferSize; i++){ 
		buffer[i] = (msg*)calloc((frame_count+1),sizeof(msg));
		if (buffer[i] == NULL){ 
			printf("Out of memory\n");
			return -1;
		}
	}

	
	//Abia dupa ce am alocat spatiu il informam pe sender ca am primit
	//informatii despre conexiune. Am ales aceasta varianta pentru siguranta, 
	//ca nu cumva sa dureze prea mult alocarea spatiului si sa se piarda 
	//cadre chiar din mesajul propriu-zis pana se termina alocarea.
	t.type = 3;
	strcpy(t.payload, windowSize);
	send_message(&t);
	
	
	printf("Filename: %s, Filesize: %i, FC: %i", filename, fs, frame_count);
	char fn[2000];

	sprintf(fn,"recv_%s", filename);
	printf("Receiving file %s of size %d\n",fn,fs);

	int fd = open(fn,O_WRONLY|O_CREAT,0644);
	if (fd<0) {
		perror("Failed to open file\n");
	}
	
  	
	//Incepem sa primim mesaje cu continutul propriu-zis
	//al mesajului, fisierului
	seq = -1;
	round = 0;
	while (fs>0){
	
		if (seq == frame_count || depl==1){ 
		//Cand s-a ajuns la nr de cadre care se trimit 
		//intr-un ciclu se reseteaza contorul de secventa
			seq = -1;
			okSubBuffer = 1;
			depl = 0;
			//Verificam toate bufferele
			for(i=0; i<=frame_count; i++){
				//daca un subBuffer contine o secventa NAK
				//el nu poate fi scris in fisier
				if (buffer[0] != NULL){
				
					if (buffer[0][i].type == 4){
						okSubBuffer = 0;
					}	
				}
				
			} 
	
				if (!okSubBuffer){
				
					pos++;
				
					reallocBuffer(pos, frame_count, &maxBufferSize, &buffer);
					
				}
				else { 
					//Inseamna ca putem scrie prima secventa si o putem elibera 
					
					//Aici e partea de scriere 
					writeSubBuffer(buffer[0], frame_count+1, fd);
					
					//Eliberarea
					free(buffer[0]); 
					for(i=0; i<maxBufferSize-1; i++){ 
						buffer[i]=buffer[i+1]; 
					}
					buffer[maxBufferSize-1] = (msg*)calloc((frame_count+1), sizeof(msg));
					
					if (!buffer[maxBufferSize-1]){
						printf("Out of memory\n"); 
						return -1;
					}
				}
		
		
		// Le marcam ca inactive pe cele care contin 4 in 
		// bufferul anterior
		markInactive(pos, frame_count, &buffer);
		
		round++;
		
		}
		
		seq++;
	
		printf("Left to read %d\n",fs);
		r = receive_message();
    	
    	
    	
		if (!r){
			perror("Receive message");
			return -1;
		}    

		if (r->type!=2){
			printf("Expecting file content\n");
			return -1;
		}
	
    
    mystrncpy(payloadContent, decode_payload(r->payload, &send_seq, &okCrc, crctable), r->len);
  
    //daca Codul CRC nu este corect inseamna ca pachetul a fost 
    //corupt, il tratam ca si cand s-a pierdut pe drum (il ignoram)  
    if (!okCrc){
  		send_seq = (seq + 1) % (frame_count +1);
  	}

    
	//A venit alt mesaj decat cel asteptat.
	//Vom ignora de acum inainte toate mesajele primite pana cand 
	//va ajunge din nou pachetul cu numarul de ordine dorit
    if (send_seq != seq) {

		//Trimitem cate mesaje de NAK este cazul sa trimitem
		//Este posibil sa se fi pierdut mai multe cadre consecutive 
		

    	if (send_seq < seq) { 
			//Se poate produce acest lucru cand ne aflam la o secventa de
			//la frontiera (secventa primita sa fie mai mica decat 
			//secventa astepta)
    		aux_seq1 = frame_count + 1 + send_seq;
    	} 
    	else { 
    		aux_seq1 = send_seq;
    	}
    	
    	okSubBuffer = 0; 
    	for(i = seq; i<aux_seq1; i++){
			
			if (i > frame_count){ 
				//Ai e tratat cazul in care se pierd cadre si inainte 
				//si dupa frontiera
				
				aux_seq2 = i % frame_count - 1;
				
				if (!okSubBuffer){
				//Daca e prima data cand trecem de frontiera trebuie 
				//sa sarim la subBufferul urmator
					okSubBuffer = 1;
					pos++; 
					
					reallocBuffer(pos, frame_count, &maxBufferSize, &buffer);
					
					markInactive(pos, frame_count, &buffer);
				}
			}
			else {
				//Altfel nu ne aflam la frontiera
				aux_seq2 = i;
			}
			//printf("SEQ %i marcat ca NAK", aux_seq2);
			if (buffer[pos][aux_seq2].type != 5){ 
				buffer[pos][aux_seq2].type = 4;
			}
			t.type = 4;
			sprintf(t.payload, "NAK%i#", aux_seq2);
			t.len = strlen(t.payload+1);
			send_message(&t);
			
			//printf("NAK pentru %i\n", aux_seq2);
		}
    	
    	if (aux_seq1 > frame_count && (aux_seq1-1) <= frame_count && !okSubBuffer) { 
    		pos++;
			
			reallocBuffer(pos, frame_count, &maxBufferSize, &buffer);
			markInactive(pos, frame_count, &buffer);
    	}    	
    	//Cadrul primit (desi gresit ca numar de secventa) il retinem
    	if (okCrc) {
			okSubBuffer = 0;
			for(i=0; i<pos; i++){
				//Atentie insa ca poate face parte din celelalte cicluri
				if (buffer[i][send_seq].type == 4) { 
					okSubBuffer = 1;
					buffer[i][send_seq].type = 3;
					mystrncpy(buffer[i][send_seq].payload, payloadContent, r->len);
					buffer[i][send_seq].len = r->len;
					//break;
					buffer[pos][send_seq].type = 5;
				}
			}

			if (!okSubBuffer) {
				buffer[pos][send_seq].type = 3;
				mystrncpy(buffer[pos][send_seq].payload, payloadContent, r->len);
				buffer[pos][send_seq].len = r->len;
			}
			
			if (r->len != -1) {
				fs -= r->len;
			}
			//fs -= 1389;
			
			//Trimitem in acest caz un ACK
			t.type = 3; 
			sprintf(t.payload, "ACK%i#", send_seq);
			t.len = strlen(t.payload+1);
			send_message(&t);
			
			seq = send_seq;
		}
		continue;
    }
    
    

    
    //A ajuns un pachet asteptat
    if (send_seq == seq) {
		
		//Tot aici trimitem ACK cand cadrul e OK

		
		if (r->len != -1){
			fs -= r->len;
		}
		
    	okSubBuffer = 0;
    	for(i=0; i<pos; i++){
    		//Atentie insa ca poate face parte celelalte cicluri
    		if (buffer[i][send_seq].type == 4) { 
    			okSubBuffer = 1;
				buffer[i][send_seq].type = 3;
				mystrncpy(buffer[i][send_seq].payload, payloadContent, r->len);
				buffer[i][send_seq].len = r->len;
				buffer[pos][send_seq].type = 5;//Cel actual il setam la nedefinit
    		}
    	}

    	if (!okSubBuffer) {
			buffer[pos][send_seq].type = 3;
			mystrncpy(buffer[pos][send_seq].payload, payloadContent, r->len);
			buffer[pos][send_seq].len = r->len;
		}
		
		
		// Trimitem in sfarsit un mesaj de tip ACK 
		t.type = 3; 
		sprintf(t.payload,"%d", seq);
		t.len = strlen(t.payload)+1;
		send_message(&t);
		continue;
	}
	
	
 	free(r);
	
  
  }
  
  //Trimitem mesajul care confirma faptul ca s-au primit
  //toate cadrele asteptate
  t.type = 6; 
  send_message(&t);
  
  
  //scriem in fisier restul bufferului 
  for(i=0; i<maxBufferSize; i++){ 
  	writeSubBuffer(buffer[i], frame_count+1, fd);
  }
  
  //Inchidem fisierul si dezalocam spatiul
  close(fd); 
  for(i=0; i<maxBufferSize; i++){ 
  	free(buffer[i]);
  }
  
  free(buffer);
  buffer = NULL;
  
  return 0;
}
