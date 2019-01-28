
void append(char *s, char c)
{
	int len = strlen(s);
	s[len] = c;
	s[len+1] = '\0';
}

