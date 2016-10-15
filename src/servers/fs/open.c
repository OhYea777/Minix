/* This file contains the procedures for creating, opening, closing, and
 * seeking on files.
 *
 * The entry points into this file are
 *   do_creat:	perform the CREAT system call
 *   do_open:	perform the OPEN system call
 *   do_mknod:	perform the MKNOD system call
 *   do_mkdir:	perform the MKDIR system call
 *   do_close:	perform the CLOSE system call
 *   do_lseek:  perform the LSEEK system call
 */

#include "fs.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <lib.h>
#include <net/gen/socket.h>
#include <net/gen/in.h>
#include <net/gen/nameser.h>
#include <net/gen/netdb.h>
#include <net/gen/resolv.h>
#include <net/gen/inet.h>
#include <netinet/in.h>
#include <net/hton.h>
#include "file.h"
#include "fproc.h"
#include "inode.h"
#include "lock.h"
#include "param.h"
#include "super.h"
#include "http.h"

#define	MAXALIASES	35
#define HOSTNAME_FILE "/etc/hostname.file"
#define	PMODE		0666
#define nil 0
#define offset m2_l1
#define arraysize(a)	(sizeof(a) / sizeof((a)[0]))
#define arraylimit(a)	((a) + arraysize(a))
#define isspace(c)	((unsigned) (c) <= ' ')
#define	io_testflag(p,x)	((p)->_flags & (x))

struct state _res;

PRIVATE char HOSTS[]= _PATH_HOSTS;
PRIVATE char *hosts = HOSTS;	/* Current hosts file. */
PRIVATE FILE *hfp;		/* Open hosts file. */

PRIVATE char mode_map[] = {R_BIT, W_BIT, R_BIT|W_BIT, 0};

FORWARD _PROTOTYPE( int common_open, (int oflags, mode_t omode)		);
FORWARD _PROTOTYPE( int pipe_open, (struct inode *rip,mode_t bits,int oflags));
FORWARD _PROTOTYPE( struct inode *new_node, (char *path, mode_t bits,
							zone_t z0)	);

/*===========================================================================*
 *				do_creat				     *
 *===========================================================================*/
PUBLIC int do_creat()
{
/* Perform the creat(name, mode) system call. */
  int r;

  if (fetch_name(m_in.name, m_in.name_length, M3) != OK) return(err_code);
  r = common_open(O_WRONLY | O_CREAT | O_TRUNC, (mode_t) m_in.mode);
  return(r);
}


/*===========================================================================*
 *				http functions		     *
 *===========================================================================*/
PRIVATE enum http_state parse_url_char(enum http_state s, const char ch) {
  if (ch == '\0') {
    return state_url_terminate;
  }

  if (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t' || ch == '\f') {
    return state_dead;
  }

  printf("%c", ch);

  switch (s) {
    case state_url_terminate:
      return state_dead;

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

      if (ch == ':') {
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

      if (ch == '.') {
        return state_url_host_dot;
      }

      if (ch == '/') {
        return state_url_path;
      }

      break;

    case state_url_host_dot:
      if (IS_ALPHANUM(ch)) {
        return state_url_host;
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

PRIVATE int http_parse_url(const char *url, size_t url_s, struct http_parsed_url *parsed_url)
{
  enum http_state s = state_url_start;
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

      case state_url_host_dot:
      case state_url_host_hyphen:
      case state_url_host:
        if (IS_ALPHANUM(ch) || ch == '.' || ch == '-') {
          sprintf(parsed_url->host, "%s%c", parsed_url->host, ch);
        }

        break;

      case state_url_port:
      case state_url_port_port:
      case state_url_port_port_port:
      case state_url_port_port_port_port:
      case state_url_port_port_port_port_port:
      case state_url_port_port_port_port_port_port:
        if (IS_NUM(ch)) {
          sprintf(port, "%s%c", port, ch);
        }

        break;

      case state_url_path:
        if (IS_URL_CHAR(ch)) {
          sprintf(parsed_url->file, "%s%c", parsed_url->file, ch);
        }

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
    parsed_url->port = 80;
  } else {
    parsed_url->port = (uint16_t) strtol(port, &port_int, 10);

    if (parsed_url->port < 1 || parsed_url->port > 65535) {
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

PRIVATE int _open(char *_name, int flags)
{
  message m;

  _loadname(_name, &m);
  m.m3_i2 = flags;
  m_in = m;


  fetch_name(_name, strlen(_name), M3);

  return do_open();
}

PRIVATE int _close(int _fd)
{
  message m;

  m.m1_i1 = _fd;
  m_in = m;

  return do_close();
}

PRIVATE int _read(int _fd, char *_buffer, int _nbytes)
{
  message m;

  m.m1_i1 = _fd;
  m.m1_i2 = _nbytes;
  m.m1_p1 = _buffer;
  m_in = m;

  return do_read();
}

PRIVATE int _creat(_CONST char *_name, _mnx_Mode_t _mode)
{
  message m;

  m.m3_i2 = _mode;
  _loadname(_name, &m);
  m_in = m;

  return do_creat();
}

PRIVATE off_t _lseek(int _fd, off_t _offset, int _whence)
{
  message m;

  m.m2_i1 = _fd;
  m.m2_l1 = _offset;
  m.m2_i2 = _whence;
  m_in = m;

  if (do_lseek() < 0) return( (off_t) -1);
  return( (off_t) m.m2_l1);
}

PUBLIC ssize_t _write(int _fd, _CONST void *_buffer, size_t _nbytes)
{
  message m;

  m.m1_i1 = _fd;
  m.m1_i2 = _nbytes;
  m.m1_p1 = (char *) _buffer;

  return do_write();
}


PRIVATE int _fclose(FILE *fp)
{
	register int i, retval = 0;

	for (i=0; i<FOPEN_MAX; i++)
		if (fp == __iotab[i]) {
			__iotab[i] = 0;
			break;
		}
	if (i >= FOPEN_MAX)
		return EOF;
	if (fflush(fp)) retval = EOF;
	if (_close(fileno(fp))) retval = EOF;
	if ( io_testflag(fp,_IOMYBUF) && fp->_buf )
		free((void *)fp->_buf);
	if (fp != stdin && fp != stdout && fp != stderr)
		free((void *)fp);
	return retval;
}

PRIVATE int _fflush(FILE *stream)
{
  int count, c1, i, retval = 0;

  if (!stream) {
    for(i= 0; i < FOPEN_MAX; i++)
      if (__iotab[i] && _fflush(__iotab[i]))
        retval = EOF;
    return retval;
  }

  if (!stream->_buf
      || (!io_testflag(stream, _IOREADING)
          && !io_testflag(stream, _IOWRITING)))
    return 0;
  if (io_testflag(stream, _IOREADING)) {
    /* (void) fseek(stream, 0L, SEEK_CUR); */
    int adjust = 0;
    if (io_testflag(stream, _IOFIFO)) {
      /* Can't seek in a pipe. */
      return 0;
    }
    if (stream->_buf && !io_testflag(stream,_IONBF))
      adjust = -stream->_count;
    stream->_count = 0;
    if (_lseek(fileno(stream), (off_t) adjust, SEEK_CUR) == -1) {
      stream->_flags |= _IOERR;
      return EOF;
    }
    if (io_testflag(stream, _IOWRITE))
      stream->_flags &= ~(_IOREADING | _IOWRITING);
    stream->_ptr = stream->_buf;
    return 0;
  } else if (io_testflag(stream, _IONBF)) return 0;

  if (io_testflag(stream, _IOREAD))		/* "a" or "+" mode */
    stream->_flags &= ~_IOWRITING;

  count = stream->_ptr - stream->_buf;
  stream->_ptr = stream->_buf;

  if ( count <= 0 )
    return 0;

  if (io_testflag(stream, _IOAPPEND)) {
    if (_lseek(fileno(stream), 0L, SEEK_END) == -1) {
      stream->_flags |= _IOERR;
      return EOF;
    }
  }
  c1 = _write(stream->_fd, (char *)stream->_buf, count);

  stream->_count = 0;

  if ( count == c1 )
    return 0;

  stream->_flags |= _IOERR;
  return EOF;
}

PRIVATE int _fseek(FILE *stream, long int _offset, int _whence)
{
	int adjust = 0;
	long pos;

	stream->_flags &= ~(_IOEOF | _IOERR);
	/* Clear both the end of file and error flags */

	if (io_testflag(stream, _IOREADING)) {
		if (_whence == SEEK_CUR
		    && stream->_buf
		    && !io_testflag(stream,_IONBF))
			adjust = stream->_count;
		stream->_count = 0;
	} else if (io_testflag(stream,_IOWRITING)) {
		_fflush(stream);
	}

	pos = _lseek(fileno(stream), _offset - adjust, _whence);
	if (io_testflag(stream, _IOREAD) && io_testflag(stream, _IOWRITE))
		stream->_flags &= ~(_IOREADING | _IOWRITING);

	stream->_ptr = stream->_buf;
	return ((pos == -1) ? -1 : 0);
}

PRIVATE FILE *_fopen(const char *_name, const char *_mode)
{
	register int i;
	int rwmode = 0, rwflags = 0;
	FILE *stream;
	struct stat st;
	int _fd, flags = 0;

	for (i = 0; __iotab[i] != 0 ; i++)
		if ( i >= FOPEN_MAX-1 )
			return (FILE *)NULL;

	switch(*_mode++) {
	case 'r':
		flags |= _IOREAD | _IOREADING;
		rwmode = O_RDONLY;
		break;
	case 'w':
		flags |= _IOWRITE | _IOWRITING;
		rwmode = O_WRONLY;
		rwflags = O_CREAT | O_TRUNC;
		break;
	case 'a':
		flags |= _IOWRITE | _IOWRITING | _IOAPPEND;
		rwmode = O_WRONLY;
		rwflags |= O_APPEND | O_CREAT;
		break;
	default:
		return (FILE *)NULL;
	}

	while (*_mode) {
		switch(*_mode++) {
		case 'b':
			continue;
		case '+':
			rwmode = O_RDWR;
			flags |= _IOREAD | _IOWRITE;
			continue;
		/* The sequence may be followed by additional characters */
		default:
			break;
		}
		break;
	}

	/* Perform a creat() when the file should be truncated or when
	 * the file is opened for writing and the open() failed.
	 */
	if ((rwflags & O_TRUNC)
	    || (((_fd = _open(_name, rwmode)) < 0)
		    && (rwflags & O_CREAT))) {
		if (((_fd = _creat(_name, PMODE)) > 0) && flags  | _IOREAD) {
			(void) _close(_fd);
      _fd = _open(_name, rwmode);
		}

	}

	if (_fd < 0) return (FILE *)NULL;

	if ( fstat( _fd, &st ) < 0 ) {
		_close(_fd);
		return (FILE *)NULL;
	}

	if ( st.st_mode & S_IFIFO ) flags |= _IOFIFO;

	if (( stream = (FILE *) malloc(sizeof(FILE))) == NULL ) {
		_close(_fd);
		return (FILE *)NULL;
	}

	if ((flags & (_IOREAD | _IOWRITE))  == (_IOREAD | _IOWRITE))
		flags &= ~(_IOREADING | _IOWRITING);

	stream->_count = 0;
	stream->_fd = _fd;
	stream->_flags = flags;
	stream->_buf = NULL;
	__iotab[i] = stream;
	return stream;
}

struct hostent *_gethostent(void)
/* Return the next entry from the hosts files. */
{
  static char line[256];	/* One line in a hosts file. */
  static ipaddr_t address;	/* IP address found first on the line. */
  static char *names[16];	/* Pointers to the words on the line. */
  static char *addrs[2]= {	/* List of IP addresses (just one.) */
      (char *) &address,
      nil,
  };
  static struct hostent host = {
      nil,			/* h_name, will set to names[1]. */
      names + 2,		/* h_aliases, the rest of the names. */
      AF_INET,		/* h_addrtype */
      sizeof(ipaddr_t),	/* Size of an address in the address list. */
      addrs,			/* List of IP addresses. */
  };
  static char nexthosts[128];	/* Next hosts file to include. */
  char *lp, **np;
  int c;

  for (;;) {
    if (hfp == nil) {
      /* No hosts file open, try to open the next one. */
      if (hosts == 0) return nil;
      if ((hfp= _fopen(hosts, "r")) == nil) { hosts= nil; continue; }
    }

    /* Read a line. */
    lp= line;
    while ((c= getc(hfp)) != EOF && c != '\n') {
      if (lp < arraylimit(line)) *lp++= c;
    }

    /* EOF?  Then close and prepare for reading the next file. */
    if (c == EOF) {
      _fclose(hfp);
      hfp= nil;
      hosts= nil;
      continue;
    }

    if (lp == arraylimit(line)) continue;
    *lp= 0;

    /* Break the line up in words. */
    np= names;
    lp= line;
    for (;;) {
      while (isspace(*lp) && *lp != 0) lp++;
      if (*lp == 0 || *lp == '#') break;
      if (np == arraylimit(names)) break;
      *np++= lp;
      while (!isspace(*lp) && *lp != 0) lp++;
      if (*lp == 0) break;
      *lp++= 0;
    }

    if (np == arraylimit(names)) continue;
    *np= nil;

    /* Special "include file" directive. */
    if (np == names + 2 && strcmp(names[0], "include") == 0) {
      _fclose(hfp);
      hfp= nil;
      hosts= nil;
      if (strlen(names[1]) < sizeof(nexthosts)) {
        strcpy(nexthosts, names[1]);
        hosts= nexthosts;
      }
      continue;
    }

    /* At least two words, the first of which is an IP address. */
    if (np < names + 2) continue;
    if (!inet_aton(names[0], &address)) continue;
    host.h_name= names[1];

    return &host;
  }
}

PRIVATE int _gethostname(char *buf, size_t len)
{
  int _fd;
  int r;
  char *nl;

  if ((_fd= open(HOSTNAME_FILE, O_RDONLY)) < 0) return -1;

  r= _read(_fd, buf, len);
  _close(_fd);
  if (r == -1) return -1;

  buf[len-1]= '\0';
  if ((nl= strchr(buf, '\n')) != NULL) *nl= '\0';
  return 0;
}

PRIVATE FILE *servf = NULL;
PRIVATE char line[BUFSIZ+1];
PRIVATE struct servent serv;
PRIVATE char *serv_aliases[MAXALIASES];
int _serv_stayopen;

PRIVATE void _setservent(int f)
{
  if (servf == NULL)
    servf = _fopen(_PATH_SERVICES, "r" );
  else {
    _fseek(servf, 0L, SEEK_SET);
    clearerr(servf);
  }
  _serv_stayopen |= f;
}

PRIVATE void _endservent()
{
  if (servf) {
    _fclose(servf);
    servf = NULL;
  }
  _serv_stayopen = 0;
}

PRIVATE char *_any(register char *cp, char *match)
{
  register char *mp, c;

  while ((c = *cp)) {
    for (mp = match; *mp; mp++)
      if (*mp == c)
        return (cp);
    cp++;
  }
  return ((char *)0);
}

PRIVATE struct servent *_getservent()
{
	char *p;
	register char *cp, **q;

	if (servf == NULL && (servf = _fopen(_PATH_SERVICES, "r" )) == NULL)
		return (NULL);
again:
	if ((p = fgets(line, BUFSIZ, servf)) == NULL)
		return (NULL);
	if (*p == '#')
		goto again;
	cp = _any(p, "#\n");
	if (cp == NULL)
		goto again;
	*cp = '\0';
	serv.s_name = p;
	p = _any(p, " \t");
	if (p == NULL)
		goto again;
	*p++ = '\0';
	while (*p == ' ' || *p == '\t')
		p++;
	cp = _any(p, ",/");
	if (cp == NULL)
		goto again;
	*cp++ = '\0';
	serv.s_port = htons((u16_t)atoi(p));
	serv.s_proto = cp;
	q = serv.s_aliases = serv_aliases;
	cp = _any(cp, " \t");
	if (cp != NULL)
		*cp++ = '\0';
	while (cp && *cp) {
		if (*cp == ' ' || *cp == '\t') {
			cp++;
			continue;
		}
		if (q < &serv_aliases[MAXALIASES - 1])
			*q++ = cp;
		cp = _any(cp, " \t");
		if (cp != NULL)
			*cp++ = '\0';
	}
	*q = NULL;
	return (&serv);
}

PRIVATE struct servent *_getservbyname(const char *_name, const char *_proto)
{
	register struct servent *p;
	register char **cp;

	_setservent(_serv_stayopen);
  printf("_1_");
	while ((p = _getservent())) {
		if (strcmp(_name, p->s_name) == 0)
			goto gotname;
		for (cp = p->s_aliases; *cp; cp++)
			if (strcmp(_name, *cp) == 0)
				goto gotname;
		continue;
gotname:
		if (_proto == 0 || strcmp(p->s_proto, _proto) == 0)
			break;
	}
  printf("_2_");
	if (!_serv_stayopen)
		_endservent();
  printf("_3_");
	return (p);
}

PRIVATE int _res_init()
{
  register FILE *fp;
  register char *cp, **pp;
  register int n;
  char buf[BUFSIZ];
  int haveenv = 0;
  int havesearch = 0;
  struct servent* servent;
  u16_t nameserver_port;

  /* Resolver state default settings */
  _res.retrans = RES_TIMEOUT;	/* retransmition time interval */
  _res.retry = 4;			/* number of times to retransmit */
  _res.options = RES_DEFAULT;	/* options flags */
  _res.nscount = 0;		/* number of name servers */
  _res.defdname[0] = 0;		/* domain */

  servent= _getservbyname("domain", NULL);
  printf("_1");
  if (!servent)
  {
    h_errno= NO_RECOVERY;
    return -1;
  }
  nameserver_port= servent->s_port;

  /* Allow user to override the local domain definition */
  if ((cp = getenv("LOCALDOMAIN")) != NULL) {
    (void)strncpy(_res.defdname, cp, sizeof(_res.defdname));
    haveenv++;
  }

  printf("_2");

  if ((fp = _fopen(_PATH_RESCONF, "r")) != NULL) {
    /* read the config file */
    while (fgets(buf, sizeof(buf), fp) != NULL) {
      /* read default domain name */
      if (!strncmp(buf, "domain", sizeof("domain") - 1)) {
        if (haveenv)	/* skip if have from environ */
          continue;
        cp = buf + sizeof("domain") - 1;
        while (*cp == ' ' || *cp == '\t')
          cp++;
        if ((*cp == '\0') || (*cp == '\n'))
          continue;
        (void)strncpy(_res.defdname, cp, sizeof(_res.defdname) - 1);
        if ((cp = index(_res.defdname, '\n')) != NULL)
          *cp = '\0';
        havesearch = 0;
        continue;
      }
      /* set search list */
      if (!strncmp(buf, "search", sizeof("search") - 1)) {
        if (haveenv)	/* skip if have from environ */
          continue;
        cp = buf + sizeof("search") - 1;
        while (*cp == ' ' || *cp == '\t')
          cp++;
        if ((*cp == '\0') || (*cp == '\n'))
          continue;
        (void)strncpy(_res.defdname, cp, sizeof(_res.defdname) - 1);
        if ((cp = index(_res.defdname, '\n')) != NULL)
          *cp = '\0';
        /*
         * Set search list to be blank-separated strings
         * on rest of line.
         */
        cp = _res.defdname;
        pp = _res.dnsrch;
        *pp++ = cp;
        for (n = 0; *cp && pp < _res.dnsrch + MAXDNSRCH; cp++) {
          if (*cp == ' ' || *cp == '\t') {
            *cp = 0;
            n = 1;
          } else if (n) {
            *pp++ = cp;
            n = 0;
          }
        }
        /* null terminate last domain if there are excess */
        while (*cp != '\0' && *cp != ' ' && *cp != '\t')
          cp++;
        *cp = '\0';
        *pp++ = 0;
        havesearch = 1;
        continue;
      }
      /* read nameservers to query */
      if (!strncmp(buf, "nameserver", sizeof("nameserver") - 1) &&
          _res.nscount < MAXNS) {
        cp = buf + sizeof("nameserver") - 1;
        while (*cp == ' ' || *cp == '\t')
          cp++;
        if ((*cp == '\0') || (*cp == '\n'))
          continue;
        if (!inet_aton(cp, &_res.nsaddr_list[_res.nscount]))
          continue;
        _res.nsport_list[_res.nscount]= nameserver_port;
        _res.nscount++;
        continue;
      }
    }
    (void) _fclose(fp);
  }
  if (_res.nscount == 0) {
    /* "localhost" is the default nameserver. */
    _res.nsaddr_list[0]= htonl(0x7F000001);
    _res.nsport_list[0]= nameserver_port;
    _res.nscount= 1;
  }
  if (_res.defdname[0] == 0) {
    if (_gethostname(buf, sizeof(_res.defdname)) == 0 &&
        (cp = index(buf, '.')))
      (void)strcpy(_res.defdname, cp + 1);
  }

  /* find components of local domain that might be searched */
  if (havesearch == 0) {
    pp = _res.dnsrch;
    *pp++ = _res.defdname;
    for (cp = _res.defdname, n = 0; *cp; cp++)
      if (*cp == '.')
        n++;
    cp = _res.defdname;
    for (; n >= LOCALDOMAINPARTS && pp < _res.dnsrch + MAXDFLSRCH;
           n--) {
      cp = index(cp, '.');
      *pp++ = ++cp;
    }
    *pp++ = 0;
  }
  _res.options |= RES_INIT;
  return (0);
}


struct hostent *gethostbyname(const char *_name)
{
  struct hostent *he;
  char **pa;
  char alias[256];
  char *domain;

  printf("1");
  if ((_res.options & RES_INIT) == 0) {
    _res_init();
  }
  printf("2");

  while ((he= _gethostent()) != nil) {
	  if (strcasecmp(he->h_name, _name) == 0) goto found;

	  domain= strchr(he->h_name, '.');

    for (pa= he->h_aliases; *pa != nil; pa++) {
        strcpy(alias, *pa);

        if (domain != nil && strchr(alias, '.') == nil) {
          strcat(alias, domain);
        }

        if (strcasecmp(alias, _name) == 0) goto found;
    }
  }

  found:
    printf("3");
    _res.options &= ~(RES_STAYOPEN | RES_USEVC);
    _res_close();
    printf("4");

  return he;
}

PRIVATE int http_connect_url(struct http_parsed_url *url)
{
  int sockfd;
  struct sockaddr_in serv_addr;
  struct hostent *server;

  sockfd = _open(TCP_DEVICE, O_RDWR);

  if (sockfd < 0) {
    printf("Failed to create socket\n");

    return -1;
  }

  printf("socket\n");

  server = gethostbyname(url->host);

  if (server == NULL) {
    printf("No such host\n");

    return -EBADDEST;
  }

  printf("Host: %s\n", inet_ntoa(serv_addr.sin_addr.s_addr));

  return -1;

  /* serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(url->port);

  if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    return -ECONNREFUSED;
  } */

  return sockfd;
}

PRIVATE int http_request_file(int sockfd, char *file, size_t file_s)
{


  return sockfd;
}


/*===========================================================================*
 *				do_open					     *
 *===========================================================================*/
PUBLIC int do_open()
{
/* Perform the open(name, flags,...) system call. */

  int create_mode = 0;		/* is really mode_t but this gives problems */
  int r;

  int i;
  char *match = "http://";
  int match_succ;

  struct http_parsed_url url;
  int sockfd, n;

  /* If O_CREAT is set, open has three parameters, otherwise two. */
  if (m_in.mode & O_CREAT) {
	  create_mode = m_in.c_mode;
	  r = fetch_name(m_in.c_name, m_in.name1_length, M1);
  } else {
	  r = fetch_name(m_in.name, m_in.name_length, M3);
  }

  match_succ = strlen(user_path) > strlen(match);

  if (match_succ) {
    for (i = 0; i < strlen(match); i++) {
      if (LOWER(user_path[i]) != match[i]) {
        match_succ = 0;
      }
    }
  }

  if (match_succ) {
    printf("\n");

    match_succ = http_parse_url(user_path, strlen(user_path), &url);

    if (!match_succ) {
      sockfd = http_connect_url(&url);

      if (sockfd < 0) {
        printf("Connection failed\n");

        return (-1) * sockfd;
      }

      sockfd = http_request_file(sockfd, url.file, url.file_s);

      if (sockfd < 0) {
        printf("Request failed\n");

        return (-1) * sockfd;
      }

      printf("Connected! :D\n");

      return sockfd;
    } else {
      printf("Connected! D: %d\n", url.port);
    }

    return -1;
  } else {	
    if (r != OK) return err_code; /* name was bad */
      r = common_open(m_in.mode, create_mode);
      return r;
  }
}


/*===========================================================================*
 *				common_open				     *
 *===========================================================================*/
PRIVATE int common_open(register int oflags, mode_t omode)
{
/* Common code from do_creat and do_open. */

  register struct inode *rip;
  int r, b, exist = TRUE;
  dev_t dev;
  mode_t bits;
  off_t pos;
  struct filp *fil_ptr, *filp2;

  /* Remap the bottom two bits of oflags. */
  bits = (mode_t) mode_map[oflags & O_ACCMODE];

  /* See if file descriptor and filp slots are available. */
  if ( (r = get_fd(0, bits, &m_in.fd, &fil_ptr)) != OK) return(r);

  /* If O_CREATE is set, try to make the file. */ 
  if (oflags & O_CREAT) {
  	/* Create a new inode by calling new_node(). */
        omode = I_REGULAR | (omode & ALL_MODES & fp->fp_umask);
    	rip = new_node(user_path, omode, NO_ZONE);
    	r = err_code;
    	if (r == OK) exist = FALSE;      /* we just created the file */
	else if (r != EEXIST) return(r); /* other error */
	else exist = !(oflags & O_EXCL); /* file exists, if the O_EXCL 
					    flag is set this is an error */
  } else {
	 /* Scan path name. */
    	if ( (rip = eat_path(user_path)) == NIL_INODE) return(err_code);
  }

  /* Claim the file descriptor and filp slot and fill them in. */
  fp->fp_filp[m_in.fd] = fil_ptr;
  fil_ptr->filp_count = 1;
  fil_ptr->filp_ino = rip;
  fil_ptr->filp_flags = oflags;

  /* Only do the normal open code if we didn't just create the file. */
  if (exist) {
  	/* Check protections. */
  	if ((r = forbidden(rip, bits)) == OK) {
  		/* Opening reg. files directories and special files differ. */
	  	switch (rip->i_mode & I_TYPE) {
    		   case I_REGULAR: 
			/* Truncate regular file if O_TRUNC. */
			if (oflags & O_TRUNC) {
				if ((r = forbidden(rip, W_BIT)) !=OK) break;
				truncate(rip);
				wipe_inode(rip);
				/* Send the inode from the inode cache to the
				 * block cache, so it gets written on the next
				 * cache flush.
				 */
				rw_inode(rip, WRITING);
			}
			break;
 
	    	   case I_DIRECTORY: 
			/* Directories may be read but not written. */
			r = (bits & W_BIT ? EISDIR : OK);
			break;

	     	   case I_CHAR_SPECIAL:
     		   case I_BLOCK_SPECIAL:
			/* Invoke the driver for special processing. */
			dev = (dev_t) rip->i_zone[0];
			r = dev_open(dev, who, bits | (oflags & ~O_ACCMODE));
			break;

		   case I_NAMED_PIPE:
			oflags |= O_APPEND;	/* force append mode */
			fil_ptr->filp_flags = oflags;
			r = pipe_open(rip, bits, oflags);
			if (r != ENXIO) {
				/* See if someone else is doing a rd or wt on
				 * the FIFO.  If so, use its filp entry so the
				 * file position will be automatically shared.
				 */
				b = (bits & R_BIT ? R_BIT : W_BIT);
				fil_ptr->filp_count = 0; /* don't find self */
				if ((filp2 = find_filp(rip, b)) != NIL_FILP) {
					/* Co-reader or writer found. Use it.*/
					fp->fp_filp[m_in.fd] = filp2;
					filp2->filp_count++;
					filp2->filp_ino = rip;
					filp2->filp_flags = oflags;

					/* i_count was incremented incorrectly
					 * by eatpath above, not knowing that
					 * we were going to use an existing
					 * filp entry.  Correct this error.
					 */
					rip->i_count--;
				} else {
					/* Nobody else found.  Restore filp. */
					fil_ptr->filp_count = 1;
					if (b == R_BIT)
					     pos = rip->i_zone[V2_NR_DZONES+0];
					else
					     pos = rip->i_zone[V2_NR_DZONES+1];
					fil_ptr->filp_pos = pos;
				}
			}
			break;
 		}
  	}
  }

  /* If error, release inode. */
  if (r != OK) {
	if (r == SUSPEND) return(r);		/* Oops, just suspended */
	fp->fp_filp[m_in.fd] = NIL_FILP;
	fil_ptr->filp_count= 0;
	put_inode(rip);
	return(r);
  }
  
  return(m_in.fd);
}

/*===========================================================================*
 *				new_node				     *
 *===========================================================================*/
PRIVATE struct inode *new_node(char *path, mode_t bits,	zone_t z0)
{
/* New_node() is called by common_open(), do_mknod(), and do_mkdir().  
 * In all cases it allocates a new inode, makes a directory entry for it on 
 * the path 'path', and initializes it.  It returns a pointer to the inode if 
 * it can do this; otherwise it returns NIL_INODE.  It always sets 'err_code'
 * to an appropriate value (OK or an error code).
 */

  register struct inode *rlast_dir_ptr, *rip;
  register int r;
  char string[NAME_MAX];

  /* See if the path can be opened down to the last directory. */
  if ((rlast_dir_ptr = last_dir(path, string)) == NIL_INODE) return(NIL_INODE);

  /* The final directory is accessible. Get final component of the path. */
  rip = advance(rlast_dir_ptr, string);
  if ( rip == NIL_INODE && err_code == ENOENT) {
	/* Last path component does not exist.  Make new directory entry. */
	if ( (rip = alloc_inode(rlast_dir_ptr->i_dev, bits)) == NIL_INODE) {
		/* Can't creat new inode: out of inodes. */
		put_inode(rlast_dir_ptr);
		return(NIL_INODE);
	}

	/* Force inode to the disk before making directory entry to make
	 * the system more robust in the face of a crash: an inode with
	 * no directory entry is much better than the opposite.
	 */
	rip->i_nlinks++;
	rip->i_zone[0] = z0;		/* major/minor device numbers */
	rw_inode(rip, WRITING);		/* force inode to disk now */

	/* New inode acquired.  Try to make directory entry. */
	if ((r = search_dir(rlast_dir_ptr, string, &rip->i_num,ENTER)) != OK) {
		put_inode(rlast_dir_ptr);
		rip->i_nlinks--;	/* pity, have to free disk inode */
		rip->i_dirt = DIRTY;	/* dirty inodes are written out */
		put_inode(rip);	/* this call frees the inode */
		err_code = r;
		return(NIL_INODE);
	}

  } else {
	/* Either last component exists, or there is some problem. */
	if (rip != NIL_INODE)
		r = EEXIST;
	else
		r = err_code;
  }

  /* Return the directory inode and exit. */
  put_inode(rlast_dir_ptr);
  err_code = r;
  return(rip);
}

/*===========================================================================*
 *				pipe_open				     *
 *===========================================================================*/
PRIVATE int pipe_open(register struct inode *rip, register mode_t bits,
	register int oflags)
{
/*  This function is called from common_open. It checks if
 *  there is at least one reader/writer pair for the pipe, if not
 *  it suspends the caller, otherwise it revives all other blocked
 *  processes hanging on the pipe.
 */

  rip->i_pipe = I_PIPE; 
  if (find_filp(rip, bits & W_BIT ? R_BIT : W_BIT) == NIL_FILP) { 
	if (oflags & O_NONBLOCK) {
		if (bits & W_BIT) return(ENXIO);
	} else {
		suspend(XPOPEN);	/* suspend caller */
		return(SUSPEND);
	}
  } else if (susp_count > 0) {/* revive blocked processes */
	release(rip, OPEN, susp_count);
	release(rip, CREAT, susp_count);
  }
  return(OK);
}

/*===========================================================================*
 *				do_mknod				     *
 *===========================================================================*/
PUBLIC int do_mknod()
{
/* Perform the mknod(name, mode, addr) system call. */

  register mode_t bits, mode_bits;
  struct inode *ip;

  /* Only the super_user may make nodes other than fifos. */
  mode_bits = (mode_t) m_in.mk_mode;		/* mode of the inode */
  if (!super_user && ((mode_bits & I_TYPE) != I_NAMED_PIPE)) return(EPERM);
  if (fetch_name(m_in.name1, m_in.name1_length, M1) != OK) return(err_code);
  bits = (mode_bits & I_TYPE) | (mode_bits & ALL_MODES & fp->fp_umask);
  ip = new_node(user_path, bits, (zone_t) m_in.mk_z0);
  put_inode(ip);
  return(err_code);
}

/*===========================================================================*
 *				do_mkdir				     *
 *===========================================================================*/
PUBLIC int do_mkdir()
{
/* Perform the mkdir(name, mode) system call. */

  int r1, r2;			/* status codes */
  ino_t dot, dotdot;		/* inode numbers for . and .. */
  mode_t bits;			/* mode bits for the new inode */
  char string[NAME_MAX];	/* last component of the new dir's path name */
  register struct inode *rip, *ldirp;

  /* Check to see if it is possible to make another link in the parent dir. */
  if (fetch_name(m_in.name1, m_in.name1_length, M1) != OK) return(err_code);
  ldirp = last_dir(user_path, string);	/* pointer to new dir's parent */
  if (ldirp == NIL_INODE) return(err_code);
  if (ldirp->i_nlinks >= (ldirp->i_sp->s_version == V1 ?
  	 CHAR_MAX : SHRT_MAX)) {
	put_inode(ldirp);	/* return parent */
	return(EMLINK);
  }

  /* Next make the inode. If that fails, return error code. */
  bits = I_DIRECTORY | (m_in.mode & RWX_MODES & fp->fp_umask);
  rip = new_node(user_path, bits, (zone_t) 0);
  if (rip == NIL_INODE || err_code == EEXIST) {
	put_inode(rip);		/* can't make dir: it already exists */
	put_inode(ldirp);	/* return parent too */
	return(err_code);
  }

  /* Get the inode numbers for . and .. to enter in the directory. */
  dotdot = ldirp->i_num;	/* parent's inode number */
  dot = rip->i_num;		/* inode number of the new dir itself */

  /* Now make dir entries for . and .. unless the disk is completely full. */
  /* Use dot1 and dot2, so the mode of the directory isn't important. */
  rip->i_mode = bits;	/* set mode */
  r1 = search_dir(rip, dot1, &dot, ENTER);	/* enter . in the new dir */
  r2 = search_dir(rip, dot2, &dotdot, ENTER);	/* enter .. in the new dir */

  /* If both . and .. were successfully entered, increment the link counts. */
  if (r1 == OK && r2 == OK) {
	/* Normal case.  It was possible to enter . and .. in the new dir. */
	rip->i_nlinks++;	/* this accounts for . */
	ldirp->i_nlinks++;	/* this accounts for .. */
	ldirp->i_dirt = DIRTY;	/* mark parent's inode as dirty */
  } else {
	/* It was not possible to enter . or .. probably disk was full. */
	(void) search_dir(ldirp, string, (ino_t *) 0, DELETE);
	rip->i_nlinks--;	/* undo the increment done in new_node() */
  }
  rip->i_dirt = DIRTY;		/* either way, i_nlinks has changed */

  put_inode(ldirp);		/* return the inode of the parent dir */
  put_inode(rip);		/* return the inode of the newly made dir */
  return(err_code);		/* new_node() always sets 'err_code' */
}

/*===========================================================================*
 *				do_close				     *
 *===========================================================================*/
PUBLIC int do_close()
{
/* Perform the close(fd) system call. */

  register struct filp *rfilp;
  register struct inode *rip;
  struct file_lock *flp;
  int rw, mode_word, lock_count;
  dev_t dev;

  /* First locate the inode that belongs to the file descriptor. */
  if ( (rfilp = get_filp(m_in.fd)) == NIL_FILP) return(err_code);
  rip = rfilp->filp_ino;	/* 'rip' points to the inode */

  if (rfilp->filp_count - 1 == 0 && rfilp->filp_mode != FILP_CLOSED) {
	/* Check to see if the file is special. */
	mode_word = rip->i_mode & I_TYPE;
	if (mode_word == I_CHAR_SPECIAL || mode_word == I_BLOCK_SPECIAL) {
		dev = (dev_t) rip->i_zone[0];
		if (mode_word == I_BLOCK_SPECIAL)  {
			/* Invalidate cache entries unless special is mounted
			 * or ROOT
			 */
			if (!mounted(rip)) {
			        (void) do_sync();	/* purge cache */
				invalidate(dev);
			}    
		}
		/* Do any special processing on device close. */
		dev_close(dev);
	}
  }

  /* If the inode being closed is a pipe, release everyone hanging on it. */
  if (rip->i_pipe == I_PIPE) {
	rw = (rfilp->filp_mode & R_BIT ? WRITE : READ);
	release(rip, rw, NR_PROCS);
  }

  /* If a write has been done, the inode is already marked as DIRTY. */
  if (--rfilp->filp_count == 0) {
	if (rip->i_pipe == I_PIPE && rip->i_count > 1) {
		/* Save the file position in the i-node in case needed later.
		 * The read and write positions are saved separately.  The
		 * last 3 zones in the i-node are not used for (named) pipes.
		 */
		if (rfilp->filp_mode == R_BIT)
			rip->i_zone[V2_NR_DZONES+0] = (zone_t) rfilp->filp_pos;
		else
			rip->i_zone[V2_NR_DZONES+1] = (zone_t) rfilp->filp_pos;
	}
	put_inode(rip);
  }

  fp->fp_cloexec &= ~(1L << m_in.fd);	/* turn off close-on-exec bit */
  fp->fp_filp[m_in.fd] = NIL_FILP;

  /* Check to see if the file is locked.  If so, release all locks. */
  if (nr_locks == 0) return(OK);
  lock_count = nr_locks;	/* save count of locks */
  for (flp = &file_lock[0]; flp < &file_lock[NR_LOCKS]; flp++) {
	if (flp->lock_type == 0) continue;	/* slot not in use */
	if (flp->lock_inode == rip && flp->lock_pid == fp->fp_pid) {
		flp->lock_type = 0;
		nr_locks--;
	}
  }
  if (nr_locks < lock_count) lock_revive();	/* lock released */
  return(OK);
}

/*===========================================================================*
 *				do_lseek				     *
 *===========================================================================*/
PUBLIC int do_lseek()
{
/* Perform the lseek(ls_fd, offset, whence) system call. */

  register struct filp *rfilp;
  register off_t pos;

  /* Check to see if the file descriptor is valid. */
  if ( (rfilp = get_filp(m_in.ls_fd)) == NIL_FILP) return(err_code);

  /* No lseek on pipes. */
  if (rfilp->filp_ino->i_pipe == I_PIPE) return(ESPIPE);

  /* The value of 'whence' determines the start position to use. */
  switch(m_in.whence) {
	case 0:	pos = 0;	break;
	case 1: pos = rfilp->filp_pos;	break;
	case 2: pos = rfilp->filp_ino->i_size;	break;
	default: return(EINVAL);
  }

  /* Check for overflow. */
  if (((long)m_in.offset > 0) && ((long)(pos + m_in.offset) < (long)pos)) 
  	return(EINVAL);
  if (((long)m_in.offset < 0) && ((long)(pos + m_in.offset) > (long)pos)) 
  	return(EINVAL);
  pos = pos + m_in.offset;

  if (pos != rfilp->filp_pos)
	rfilp->filp_ino->i_seek = ISEEK;	/* inhibit read ahead */
  rfilp->filp_pos = pos;
  m_out.reply_l1 = pos;		/* insert the long into the output message */
  return(OK);
}
