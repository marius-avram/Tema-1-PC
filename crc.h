#define CRCCCITT 0x1021

#include <stdio.h>
#include <stdlib.h>

typedef unsigned short int word;
typedef unsigned char byte;

word calculcrc (word, word, word);
word* tabelcrc (word);
void crctabel (word, word*, word*);

//Functii care encodeaza si decodeaza mesajul

char* encode_payload(char[1389], int, word*); 
char* decode_payload(char[1400], int*, int*, word*);
word crcForString(char*, word*);
