
void append(char *s, char c){
	int len = strlen(s);
	s[len] = c;
	s[len+1] = '\0';
}

char *substr(char *src,int pos,int len) { 
  char *dest=NULL;                        
  if (len>0) {                  
    /* allocation et mise à zéro */          
    dest = calloc(len+1, 1);      
    /* vérification de la réussite de l'allocation*/  
    if(NULL != dest) {
      strncat(dest,src+pos,len);
    }
  }
  return dest;                            
}

int strpos(char *haystack, char *needle)
{
   char *p = strstr(haystack, needle);
   if (p)
      return p - haystack;
   return -1;
}
