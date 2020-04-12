#include <stdio.h>
#include <string.h>

int alarm_interval;
char alarm_mail[ 2048 ];
char alarm_gsm[ 10 ][ 1024 ];
int max_tries; 
int max_tries_if_coredumped;
int gsm_list_len;
void parse_conf();
char service[ 100 ] ="ui";

int main()
{
  int i;
  parse_conf();
  printf("alarm_interval : %d\n", alarm_interval );
  printf("max_tries : %d\n", max_tries );
  printf("max_tries_if_coredumped : %d\n", max_tries_if_coredumped );
  printf("gsm_list_len : %d\n", gsm_list_len );
  printf("alarm_mail : %s\n", alarm_mail );
  for ( i = 0; i < gsm_list_len; i++ )
    printf("alarm_gsm[%d] : %s\n", i, alarm_gsm[ i ] );
  return 0;
}

void parse_conf()
{
  FILE *fp;
  int len, tmp;
  int finished = 0;
  char conf_line[ 2048 ] = "";
  char service_tag[ 1024 ];
  char *p, *q;
 
  if ( snprintf( service_tag, 1024, "[%s]", service ) < 0 )
    return;
 
  if ( ( fp = fopen( "supervise.conf", "r" ) ) == NULL )
  {
    printf( "Cannot open file : supervise.conf!\n" );
    return;
  }
 
  while ( fgets( conf_line, 2048, fp ) != NULL )
  {
    if ( strncmp( conf_line, "[global]", strlen( "[global]" ) ) == 0 )
      break; 
    if ( strncmp( conf_line, service_tag, strlen( service_tag ) ) == 0 )
    {
      finished = 1;
      break;
    }
  }

  while ( fgets( conf_line, 2048, fp ) != NULL && conf_line[ 0 ] != '[' )
  {
    p = conf_line + strlen( conf_line ) - 1;
    while ( *p == '\n' || isspace( *p ) ) 
    {
      *p = 0;
      p--;
    }

    if ( strncmp( conf_line, "alarm_mail", strlen( "alarm_mail" ) ) == 0 )
    {
      p = conf_line + strlen( "alarm_mail" );
      while ( *p && isspace( *p ) )
        p++;
      if ( *p++ != ':' )
        continue;
      while ( *p && isspace( *p ) )
        p++;
      if ( *p )
        strncpy( alarm_mail, p, 1024 );
      continue;
    } 
    if ( strncmp( conf_line, "alarm_gsm", strlen( "alarm_gsm" ) ) == 0 )
    {
      p = conf_line + strlen( "alarm_gsm" );
      while ( *p && isspace( *p ) )
        p++;
      if ( *p++ != ':' )
        continue;
      while ( *p && isspace( *p ) )
        p++;
      if ( !p )
        continue;
      len = strlen( p );
      q = p;
      while ( *q )
      {
        if ( isspace( *q ) )
          *q = '\0';
          q++;
      }
      while (  len > 0 && gsm_list_len < 10 )
      {
        if ( !(*p) ) 
        {
          p++;
          len--;
          continue;
        }
        strncpy( alarm_gsm[ gsm_list_len ], p, 1024 );
        len -= strlen( alarm_gsm[ gsm_list_len ] );
        p += strlen( alarm_gsm[ gsm_list_len ] );
        gsm_list_len++;
      }
      continue;
    }   
    if ( strncmp( conf_line, "max_tries_if_coredumped", strlen( "max_tries_if_coredumped" ) ) == 0 )
    {
      sscanf( conf_line, "max_tries_if_coredumped : %d", & max_tries_if_coredumped );
      continue;
    } 
    if ( strncmp( conf_line, "max_tries", strlen( "max_tries" ) ) == 0 )
    {
      sscanf( conf_line, "max_tries : %d", &max_tries );
      continue;
    } 
    if ( strncmp( conf_line, "alarm_interval", strlen( "alarm_interval" ) ) == 0 )
    {
      sscanf( conf_line, "alarm_interval : %d", &alarm_interval );
      continue;
    } 
  }
  if ( finished )
  {
    fclose( fp );
    return;
  }

  while ( strncmp( conf_line, service_tag, strlen( service_tag ) ) != 0 && fgets( conf_line, 2048, fp ) != NULL )
    ;

  while ( fgets( conf_line, 2048, fp ) != NULL && conf_line[ 0 ] != '[' )
  {
    p = conf_line + strlen( conf_line ) - 1;
    while ( *p == '\n' || isspace( *p ) ) 
    {
      *p = 0;
      p--;
    }
    if ( strncmp( conf_line, "alarm_mail", strlen( "alarm_mail" ) ) == 0 )
    {
      p = conf_line + strlen( "alarm_mail" );
      while ( *p && isspace( *p ) )
        p++;
      if ( *p++ != ':' )
        continue;
      while ( *p && isspace( *p ) )
        p++;
      if ( *p )
        snprintf( alarm_mail + strlen( alarm_mail ), 1024, " %s", p );
      continue;
    } 

    if ( strncmp( conf_line, "alarm_gsm", strlen( "alarm_gsm" ) ) == 0 )
    {
      p = conf_line + strlen( "alarm_gsm" );
      while ( *p && isspace( *p ) )
        p++;
      if ( *p++ != ':' )
        continue;
      while ( *p && isspace( *p ) )
        p++;
      if ( !p )
        continue;
      len = strlen( p );
      q = p;
      while ( *q )
      {
        if ( isspace( *q ) )
          *q = '\0';
          q++;
      }
      while ( len > 0 && gsm_list_len < 10 )
      {
        if ( !(*p) ) 
        {
          p++;
          len--;
          continue;
        }
        strncpy( alarm_gsm[ gsm_list_len ], p, 1024 );
        len -= strlen( alarm_gsm[ gsm_list_len ] );
        p += strlen( alarm_gsm[ gsm_list_len ] );
        gsm_list_len++;
      }
      continue;
    }   
    if ( strncmp( conf_line, "max_tries_if_coredumped", strlen( "max_tries_if_coredumped" ) ) == 0 )
    {
      sscanf( conf_line, "max_tries_if_coredumped : %d", &tmp );
      if ( tmp > 0 )
        max_tries_if_coredumped = tmp;
      continue;
    } 
    if ( strncmp( conf_line, "max_tries", strlen( "max_tries" ) ) == 0 )
    {
      sscanf( conf_line, "max_tries : %d", &tmp );
      if ( tmp > 0 )
        max_tries = tmp;
      continue;
    } 
    if ( strncmp( conf_line, "alarm_interval", strlen( "alarm_interval" ) ) == 0 )
    {
      sscanf( conf_line, "alarm_interval : %d", &tmp );
      if ( tmp > 0 )
        alarm_interval = tmp;
      continue;
    } 
  }
  fclose( fp );
}
