#include "crc.h"
#include <stdio.h>
#include <string.h>

//
// calculeaza noul rest partial pe baza vechiului rest
// (acum), a unui octet de date (data) si a polinomului
// generator (genpoli)
//

word calculcrc (word data, word genpoli, word acum)
{
  int i;
  data <<= 8;
  for (i=8; i>0; i--)
     {
     if ((data^acum) & 0x8000)
       acum = (acum << 1) ^ genpoli;
     else
       acum <<= 1;
     data <<= 1;
     }
  return acum;
}


//
// calculeaza tabelul codurilor CRC
//

//
// alcatuieste tabelul codurilor CRC pentru un anumit
// polinom generator (poli); apeleaza "calculcrc" pentru
// toate cele 256 combinatii posibile ale octetului de date
// intoarce un pointer la tabelul de coduri, alocat dinamic
//

word* tabelcrc (word poli)
{
  word* ptabelcrc;
  int i;
  if ((ptabelcrc = (word*) malloc (256 * sizeof (word))) == NULL)
     return NULL;
  for (i=0; i<256; i++)
    ptabelcrc [i] = calculcrc (i, poli, 0);
  return ptabelcrc;
}

 

//
// calculeaza CRC partial (acum) corespunzator unui octet
// (data) prin utilizarea tabelului codurilor CRC (tabelcrc)
//

void crctabel (word data, word* acum, word* tabelcrc)
  {
  word valcomb;
  valcomb = ((*acum >> 8) ^ data) & 0x00FF;
  *acum = ((*acum & 0x00FF) << 8) ^ tabelcrc [valcomb];
}

//primeste un numar (numarul de secventa) si returneaza 
//un sir de caractere de dimensiune 5.
char* itoaWithLeadingZeros(unsigned short int seqNum){
	char* result = (char*)calloc(6,sizeof(char));
	char* num = (char*)calloc(6,sizeof(char)); 
	char *presult = result;
	int i;
	if (!result || !num){ 
		return NULL;
	}
	sprintf(num, "%d", seqNum);
	
	int len = strlen(num);
	for(i=0; i < 5-len; i++) { 
		*result = '0';
		result++;
	}

	
	strncat(result,num,len);
	free(num);
	
	return presult;
}


//Introduce in interiorul mesajului numarul de secventa si 
//codul CRC. Am rezervat 5 caractere pentru numarul de 
//de secventa si 5 caractere pentru CRC (avand in vedere 
//ca este un unsigned short int care poate lua valori 
//intre 0 si 65535).
//Mesajul va avea urmatoarea structura 
//SEQ|TEXT-MESAJ|CRC\0

//Nu vom avea nevoie de separatori deoarece dimenisiunea 
//campurilor auxiliare este fixa 
char* encode_payload(char payload[1389], int seqNum, word* crctable){ 
	char * result = (char*)malloc(sizeof(char)*1400), * presult = result;
	char * numb = itoaWithLeadingZeros(seqNum);
	int i;
	unsigned short int crcCode = 0;
	
	strncpy(result, numb, 5);
	
	//luam si numarul de secventa in calculul codului CRC 
	//pentru ca e posibil sa se corupa si acesta in timpul 
	//transferului de date
	for(i=0; i<5; i++){ 
		crctabel(*result, &crcCode, crctable);
		result++;
	}
	
	for(i=0; i<1389; i++){

		*result = payload[i];		
		if (payload[i] == 0) {
			*result = '1';
		}
		//Calculam codul pentru fiecare caracter in parte
		crctabel(*result, &crcCode, crctable);
		result++;
	}
	
	//printf("CRC is %i\n" , crcCode);
	free(numb);
	numb = itoaWithLeadingZeros(crcCode);
	

	strncat(result,numb,5);
	free(numb);
	
	//printf("Encoded string: %s", presult);
	
	return presult;
}

//Decodeaza un mesaj si verifica daca acesta este corupt sau nu
//(recalculand codul CRC) de asemenea introduce in parametrul 
//seqNum numarul de secventa. Cu acesta se va putea face ulterior 
//verificarea daca s-a primit pachetul asteptat sau nu.

char* decode_payload(char payload[1400], int* seqNum, int* ok, word* crctable){ 
	
	char * result = (char*)malloc(sizeof(char)*1389), * presult = result; 
	char *numb = (char*)malloc(sizeof(char)*6);
	int i;
	word crcCode = 0 ;
	strncpy(numb,payload,5);

	//printf("SEQ s = %s", numb); 
	*seqNum = atoi(numb);
	
	//printf("||SEQ NUMBER == %i||\n", *seqNum);
	for(i=0; i<5; i++){
		crctabel(payload[i], &crcCode, crctable);
	}
	
	for(i=0; i<1389; i++) { 
		*result = payload[i+5];
		crctabel(*result, &crcCode, crctable);
		result++;		
	}
	
	
	if (atoi(strncpy(numb,payload+1394,5)) == crcCode){ 
		//Inseamna ca transferul a decurs bine, deoarece 
		//crc-ul din string si cel recalculat sunt egali
		*ok = 1;
	} else {
		*ok = 0;
	}
	
	
	printf("||CRC == %i & Calculated == %i||\n", atoi(numb), crcCode);
	
	return presult;
	
} 

word crcForString(char *string, word *crctable){
	int i, n = strlen(string); 
	word crcCode = 0;
	for(i=0; i<n; i++){
		crctabel(*string, &crcCode, crctable);
		string++;
	}
	return crcCode;
}




