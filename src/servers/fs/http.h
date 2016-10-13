EXTERN struct http_parsed_url
{
  char host[100];
  size_t host_s;

  uint16_t port;

  char file[255];
  size_t file_s;
};

PRIVATE const char *VALID_URL_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~:/?#[]@!$&'()*+,;=%";

#define CR                  '\r'
#define LF                  '\n'
#define LOWER(c)            (unsigned char)(c | 0x20)
#define IS_ALPHA(c)         (LOWER(c) >= 'a' && LOWER(c) <= 'z')
#define IS_NUM(c)           ((c) >= '0' && (c) <= '9')
#define IS_ALPHANUM(c)      (IS_ALPHA(c) || IS_NUM(c))
#define IS_URL_CHAR(c)      (strchr(VALID_URL_CHARS, c))

EXTERN enum state
{
  state_dead = 1,

  state_url_start,
  state_url_path,

  state_url_schema,
  state_url_schema_slash,
  state_url_schema_slash_slash,

  state_url_host_start,
  state_url_host,
  state_url_host_hyphen,
  state_url_host_dot,
  state_url_port,
  state_url_port_port,
  state_url_port_port_port,
  state_url_port_port_port_port,
  state_url_port_port_port_port_port,
  state_url_port_port_port_port_port_port,
  state_url_terminate
};