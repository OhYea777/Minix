#include "http.h"

PRIVATE const char *VALID_URL_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~:/?#[]@!$&'()*+,;=%";

#define CR                  '\r'
#define LF                  '\n'
#define LOWER(c)            (unsigned char)(c | 0x20)
#define IS_ALPHA(c)         (LOWER(c) >= 'a' && LOWER(c) <= 'z')
#define IS_NUM(c)           ((c) >= '0' && (c) <= '9')
#define IS_ALPHANUM(c)      (IS_ALPHA(c) || IS_NUM(c))
#define IS_URL_CHAR(c)      (strchr(VALID_URL_CHARS, c))

enum state
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
  state_url_port,
  state_url_port_port,
  state_url_port_port_port,
  state_url_port_port_port_port,
  state_url_port_port_port_port_port,
  state_url_port_port_port_port_port_port,
};

// http://google.com:34/file.php?dl=true&name=Hello%20World
PRIVATE enum state parse_url_char(enum state s, const char ch) {
  if (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t' || ch == '\f') {
    return state_dead;
  }

  switch (s) {
    case state_url_start:
      if (ch == '/' || ch == '*') {
        return state_url_path;
      }

      if (IS_ALPHA(ch)) {
        return state_url_schema;
      }

      break;

    case state_url_schema:
      if (IS_ALPHA(ch)) {
        return s;
      }

      if (ch == '/') {
        return state_url_schema_slash;
      }

      break;

    case state_url_schema_slash:
      if (ch == '/') {
        return state_url_schema_slash_slash;
      }

      break;

    case state_url_schema_slash_slash:
      if (ch == '/') {
        return state_url_host_start;
      }

      break;

    case state_url_host_start:
      if (IS_ALPHANUM(ch)) {
        return state_url_host;
      }

      break;

    case state_url_host_hyphen:
      if (ch == '/' || ch == ':') {
        return state_dead;
      }
    case state_url_host:
      if (IS_ALPHANUM(ch)) {
        return state_url_host;
      }

      if (ch == '-') {
        return state_url_host_hyphen;
      }

      if (ch == ':') {
        return state_url_port;
      }

      break;

    case state_url_port:
      if (IS_NUM(ch)) {
        return state_url_port_port;
      }

      if (ch == '/') {
        return state_url_path;
      }

      break;

    case state_url_port_port:
      if (IS_NUM(ch)) {
        return state_url_port_port_port;
      }

      if (ch == '/') {
        return state_url_path;
      }

      break;

    case state_url_port_port_port:
      if (IS_NUM(ch)) {
        return state_url_port_port_port_port;
      }

      if (ch == '/') {
        return state_url_path;
      }

      break;

    case state_url_port_port_port_port:
      if (IS_NUM(ch)) {
        return state_url_port_port_port_port_port;
      }

      if (ch == '/') {
        return state_url_path;
      }

      break;

    case state_url_port_port_port_port_port:
      if (IS_NUM(ch)) {
        return state_url_port_port_port_port_port_port;
      }

      if (ch == '/') {
        return state_url_path;
      }

      break;

    case state_url_port_port_port_port_port_port:
      if (ch == '/') {
        return state_url_path;
      }

      break;

    case state_url_path:
      if (IS_URL_CHAR(ch)) {
        return state_url_path;
      }

      break;

    default:
      break;
  }

  return state_dead;
}

PUBLIC int http_parse_url(const char *url, size_t url_s, struct http_parsed_url *parsed_url)
{
  enum state s = state_url_start;
  char ch;
  int pos;
  char *port_int;
  char port[5];

  parsed_url->port = 0;

  parsed_url->host_s = 0;
  parsed_url->file_s = 0;

  parsed_url->host[0] = '\0';
  parsed_url->file[0] = '\0';

  for (pos = 0, ch = url[0]; pos < strlen(url); ch = url[++pos]) {
    s = parse_url_char(s, ch);

    switch (s) {
      case state_dead:
        return 1;

      case state_url_host_hyphen:
      case state_url_host:
        if (pos > sizeof(parsed_url->host)) {
          return 1;
        }

        parsed_url->host[pos] = ch;

        break;

      case state_url_port:
      case state_url_port_port:
      case state_url_port_port_port:
      case state_url_port_port_port_port:
      case state_url_port_port_port_port_port:
        if (!strlen(port)) {
          port[0] = ch;
        }

        port[strlen(port)] = ch;

        break;

      case state_url_path:
        if (pos > sizeof(parsed_url->file)) {
          return 1;
        }

        parsed_url->file[pos] = ch;

        break;

      default:
        break;
    }
  }

  if (!(parsed_url->host_s = strlen(parsed_url->host))) {
    return 1;
  }

  strcat(parsed_url->host, '\0');

  if (!strlen(port)) {
    parsed_url->port = HTTP_DEFAULT_PORT;
  } else {
    parsed_url->port = (unsigned int) strtol(port, &port_int, 10);

    if (*port_int == '\0' || parsed_url->port > 65535) {
      return 1;
    }
  }

  if (!(parsed_url->file_s = strlen(parsed_url->file))) {
    strcat(parsed_url->file, "/");

    parsed_url->file_s = strlen(parsed_url->file);
  }

  strcat(parsed_url->file, '\0');

  return 0;
}

PUBLIC int http_connect_url(struct http_parsed_url *url)
{
  return 0;
}