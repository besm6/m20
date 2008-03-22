/*
 * Use latin letters for GOST output.
 */
extern int gost_latin;

void gost_putc (unsigned char, FILE*);
void gost_write (unsigned char*, int, FILE*);
unsigned char unicode_to_gost (unsigned short);
unsigned char utf8_to_gost (unsigned char**);
void utf8_puts (char*, FILE*);
void wchar_puts (wchar_t*, FILE*);
int unicode_getc (FILE*);
void unicode_ungetc (int);
void set_input_encoding (char*);
