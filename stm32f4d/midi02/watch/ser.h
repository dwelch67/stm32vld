
//-----------------------------------------------------------------------------
// Copyright (c) David Welch 1996
//-----------------------------------------------------------------------------

unsigned char ser_open ( char * );
void strobedtr ( void );
void ser_close ( void );
int ser_senddata ( unsigned char *, unsigned int );
void ser_sendstring ( char *s );
void ser_update ( void );
unsigned short ser_copystring ( unsigned char * );
unsigned short ser_dump ( unsigned short );

//-----------------------------------------------------------------------------
// Copyright (c) David Welch 1996
//-----------------------------------------------------------------------------

