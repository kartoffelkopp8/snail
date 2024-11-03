#include <errno.h>
#include <netdb.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FQDN_LEN 256
#define BUFFERSIZE 64
#define UADD_LEN 256

typedef enum RETURN { CRIT_ERR, ERR, SUCCESS } RETURN;

const char *adress = "faui03.cs.fau.de";

void die(const char *message) {
  perror(message);
  exit(EXIT_FAILURE);
}

/**
 * Retrieves the fully qualified domain name (FQDN), user email address, and
 * user name.
 * @param fqdn: Buffer to store the FQDN.
 * @param Uadress: Buffer to store the user email address.
 * @param name: Buffer to store the user's real name.
 * @return: SUCCESS if successful, otherwise CRIT_ERR on error.
 */
// @Korrektur: sinnvoll innerhalb der funktion zu allocieren, oder ausserhalb?
RETURN getUserInfo(char *fqdn, char *Uadress, char *name) {
  struct addrinfo hints, *res;
  const char *ADDRESSADD = "@cip.cs.fau.de"; // domain suffix for email

  uid_t uid = getuid(); // get user uid

  // initialise hints struct
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = AI_CANONNAME;
  hints.ai_socktype = SOCK_STREAM;

  // retrieve hostname
  if (gethostname(fqdn, FQDN_LEN) == -1) {
    fprintf(stderr, "ERROR: gethostname\n");
    return CRIT_ERR;
  }

  // retrieve adress information for fqdn
  int status = getaddrinfo(fqdn, NULL, &hints, &res);
  if (status != 0) {
    fprintf(stderr, "getaddrinfo: %s", gai_strerror(status));
  }

  if (res->ai_canonname != NULL) {
    strncpy(fqdn, res->ai_canonname, FQDN_LEN);
  } else {
    fprintf(stderr, "Cannonical name ist NULL");
    freeaddrinfo(res);
    return CRIT_ERR;
  }
  freeaddrinfo(res);

  // generate email adress of user
  errno = 0;
  struct passwd *pwd;
  if ((pwd = getpwuid(uid)) == NULL) {
    if (errno != 0) {
      perror("getpwuid");
      return CRIT_ERR;
    } else {
      fprintf(stderr, "no pwd entry found");
      perror("fprintf");
      return CRIT_ERR;
    }
  }

  // format adress
  if (snprintf(Uadress, FQDN_LEN, "%s%s", pwd->pw_name, ADDRESSADD) < 0) {
    perror("snprintf");
    return CRIT_ERR;
  }

  // get Full name without date of creation
  char *nom = pwd->pw_gecos;
  char *comma = strchr(nom, ',');
  if (comma == NULL) {
    fprintf(stderr, "komma not found in %s, or not a valid user", nom);
    return CRIT_ERR;
  }
  *comma = '\0';

  // format name
  if (snprintf(name, FQDN_LEN, "%s", nom) < 0) {
    perror("snprintf");
    return CRIT_ERR;
  }

  return SUCCESS;
}

/**
 * establishes a connection to smtp server
 * @return: returns the connected socket
 */
// returns either a positive int as a socket id, or a 0< for errors
int getConnSock() {
  struct addrinfo hints, *res;
  int sock;

  // initialise addrinfo
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  // get ip adress of the relay
  int status = getaddrinfo(adress, "25", &hints, &res);
  if (status != 0) {
    fprintf(stderr, "getaddrinfo: %s", gai_strerror(status));
    return -1;
  }

  // check for valid IP
  struct addrinfo *curr;
  for (curr = res; curr != NULL; curr = curr->ai_next) {
    if ((sock = socket(curr->ai_family, curr->ai_socktype,
                       curr->ai_protocol)) == -1) {
      return -1;
    }
    // if its working, break out of loop
    if (connect(sock, curr->ai_addr, curr->ai_addrlen) == 0) {
      break;
    }
    printf("socket");
    close(sock); // TODO evtl fehlerbehandeln
  }
  if (curr == NULL) {
    fprintf(
        stderr,
        "Error: Could not find a viable address to establish a connection.\n");
    return -1;
  }
  return sock;
}

/**
 * function reads line from file pointer and stores in dynamicly allocated
 * buffer of size size
 * @param rs: FILE pointer to read from
 * @param buffer: pointer to allocated string
 * @param size: size of the buffer, updated as needed
 * @return: -1 on error, else position of last char read in the buffer
 */
// takes allocated buffer, reads in a line from filepointer and returns position
// of last char: \0
int readP(char **buffer, FILE *rs, int *size) {
  int ch;
  char c;
  int pos = 0;

  if ((*buffer) == NULL) {
    *size = BUFFERSIZE;
    if ((((*buffer)) = malloc(*size)) == NULL) {
      perror("malloc");
      return -1;
    }
  }

  // on error return EOF or if sock is closed
  while ((ch = fgetc(rs)) != EOF) {
    c = (char)ch;

    if (pos >= *size - 1) {
      *size += BUFFERSIZE;
      if (((*buffer) = realloc((*buffer), (*size))) == NULL) {
        perror("realloc");
        return -1;
      }
    }

    (*buffer)[pos++] = c;
    if (c == '\n') {
      break;
    }
  }
  if (ferror(rs)) {
    perror("fgetc");
    return -1;
  }

  (*buffer)[pos] = '\0'; // null terminate string
  return pos;
}

/**
 * writes a message to the FILE pointer/server
 * @param ws: FILE to write to
 * @param text: string to write
 * @return: -1 on error else 0
 */
int sendP(FILE *ws, const char *text) {
  if (fputs(text, ws) == EOF) {
    perror("fputs 160");
    return -1;
  }
  fflush(ws);
  return 0;
}

/**
 * function generates smtp headers for the message, must be freed after usage
 * with freeHeader
 * @param fqdn: full qualified domain name of the sender
 * @param sMail: e-mail adress of sender
 * @param eMail: e-mail adress of the user
 * @param name: Full name of the user
 * @return array of strings containing the header lines
 */
// function to generate the header arguments
char **createHeader(char *fqdn, char *sMail, const char *eMail, char *name) {
  // always add 3 \r\n\0
  char *formHELO = "HELO %s\r\n";
  char *formFROM = "MAIL FROM: %s\r\n";
  char *formTO = "RCPT TO: %s\r\n";
  char *data = "DATA\r\n";
  char *formHead = "From: %s <%s>\r\nTo: <%s>\r\n";

  char **info = calloc(5, sizeof(char *));
  if (info == NULL) {
    perror("calloc");
    return NULL;
  }

  // generate sizes for malloc
  int Helolen = snprintf(NULL, 0, formHELO, fqdn) + 1;
  int FROMlen = snprintf(NULL, 0, formFROM, sMail) + 1;
  int TOlen = snprintf(NULL, 0, formTO, eMail) + 1;
  int Headlen = snprintf(NULL, 0, formHead, name, sMail, eMail) + 1;
  if (Helolen < 0 || FROMlen < 0 || TOlen < 0 || Headlen < 0) {
    perror("snprintf");
    return NULL;
  }

  // allocate memory
  info[0] = malloc(Helolen);
  info[1] = malloc(FROMlen);
  info[2] = malloc(TOlen);
  info[3] = strdup(data);
  info[4] = malloc(Headlen);
  for (int i = 0; i < 5; i++) {
    if (info[i] == NULL) {
      perror("malloc");
      return NULL;
    }
  }

  // fill and format strings
  Helolen = snprintf(info[0], Helolen, formHELO, fqdn);
  FROMlen = snprintf(info[1], FROMlen, formFROM, sMail);
  TOlen = snprintf(info[2], TOlen, formTO, eMail);
  Headlen = snprintf(info[4], Headlen, formHead, name, sMail, eMail);
  if (Helolen < 0 || FROMlen < 0 || TOlen < 0 || Headlen < 0) {
    perror("snprintf");
    return NULL;
  }

  return info;
}

/**
 * function to free char ** from createHeader
 * @param header: pointing to array of strings to be freed
 */
void freeHeader(char **header) {
  if (header == NULL) {
    return;
  }

  for (int i = 0; i < 5; i++) {
    free(header[i]);
  }
  free(header);
}

/**
 * function sends header to server and verifies responses
 * @param fqdn: Fully qualified domain name of the sender
 * @param sMail: Sender's email address
 * @param eMail: Recipient's email address
 * @param name: Sender's name
 * @param ws: Write stream for sending data to the server
 * @param rs: Read stream for receiving data from the server
 * @return: CRIT_ERR if there is an error, SUCCESS otherwise
 */
RETURN checkSendHeader(char *fqdn, char *sMail, char *eMail, char *name,
                       FILE *ws, FILE *rs) {
  char *text = calloc(BUFFERSIZE, sizeof(char));
  int size = BUFFERSIZE;
  char **header;
  char *exp[4] = {"250", "250", "250", "354"};

  // create header
  if ((header = createHeader(fqdn, sMail, eMail, name)) == NULL) {
    return CRIT_ERR;
  }

  if (text == NULL) {
    perror("calloc");
    return CRIT_ERR;
  }

  // read server answer after connecting, check it and put it on the screen
  if (readP(&text, rs, &size) == -1) {
    return CRIT_ERR;
  }

  if (fputs(text, stdout) == EOF) {
    perror("fputs 248");
    return CRIT_ERR;
  }
  fflush(stdout);

  if (strncmp(text, "220", 3) != 0) {
    fprintf(stderr, "wrong code");
    return CRIT_ERR;
  }

  // loop for sending and Verify answers
  for (int i = 0; i < 5; i++) {
    if (fputs(header[i], stdout) == EOF) {
      perror("fputs 259");
      return CRIT_ERR;
    }

    // send
    if (sendP(ws, header[i]) == -1) {
      return CRIT_ERR;
    }
    fflush(ws);

    if (i < 4) {

      // hier stopp
      if (readP(&text, rs, &size) == -1) {
        return CRIT_ERR;
      }

      if (fputs(text, stdout) == EOF) {
        perror("fputs 275");
        return CRIT_ERR;
      }

      fflush(stdout);

      if (strncmp(text, exp[i], 3) != 0) {
        fprintf(stderr, "wrong code %d\n", i);
        return CRIT_ERR;
      }
    }
  }

  // free the header
  freeHeader(header);
  return SUCCESS;
}

/**
 * Reads a message from standard input (stdin) until a single period ('.')
 * followed by \r\n or just \n is detected. The read text is stored in a
 * dynamically allocated buffer.
 *
 * @return A pointer to the read text or NULL in case of an error.
 */
char *readBody() {
  int size = BUFFERSIZE;           // Initial size of the buffer
  int pos = 0;                     // Current position in the buffer
  int ch;                          // Variable for the current character
  char *body = malloc(BUFFERSIZE); // Allocate memory for the buffer

  // Check if the memory was successfully allocated
  if (body == NULL) {
    perror("malloc"); // Print an error message if malloc fails
    return NULL;      // Return NULL in case of an error
  }

  // Loop to read characters until EOF
  while ((ch = fgetc(stdin)) != EOF) {

    // Check if the buffer is almost full and needs space for '\0' and '\r\n'
    if (pos >= size - 1) {
      size += BUFFERSIZE; // Increase the buffer size
      // Try to reallocate the buffer
      if ((body = realloc(body, size)) == NULL) {
        perror("realloc"); // Print an error message if realloc fails
        return NULL;       // Return NULL in case of an error
      }
    }

    // Store the current character in the buffer
    body[pos++] = (char)ch;
  }
  if (pos > 0) {
    // Null-terminate the string.
    body[pos] = '\0';
  }

  // Check if any errors occurred while reading from stdin
  if (ferror(stdin)) {
    perror("fgetc"); // Print an error message
    free(body);      // Free allocated memory
    return NULL;     // Return NULL in case of an error
  }

  return body; // Return the read text
}

/**
 * Encodes the given message by escaping periods at the beginning of lines
 * and converting all line breaks to \r\n.
 *
 * @param msg The input message to encode.
 * @return A pointer to the encoded string or NULL in case of an error.
 */
char *encode(char *msg) {
  int pos = 0;
  char *encoded;
  char prev = '\0';

  if (msg == NULL) {
    fprintf(stderr, "body is NULL");
    return NULL;
  }
  // guess size for a start and allocate it
  int size = strlen(msg) * 2 + 1;
  encoded = calloc(size, sizeof(char));
  if (encoded == NULL) {
    perror("calloc");
    return NULL;
  }

  for (int i = 0; i < size; i++) {
    char ch = msg[i];

    if (ch == '.') {
      encoded[pos++] = ch;
      encoded[pos++] = ch;

    } else if (ch == '\n') {
      // if previous char not a \r -> write
      if (prev != '\r') {
        encoded[pos++] = '\r';
      }
      encoded[pos++] = '\n';

    } else {
      encoded[pos++] = ch;
    }

    prev = ch;
  }

  encoded[pos] = '\0';

  free(msg);
  return encoded;
}

/**
 * Send "QUIT" command to the server to end SMTP session
 * @param ws: Write stream to the server
 * @param rs: Read stream from the server
 * @return: -1 if error occurs, 0 on success
 */
int quit(FILE *ws, FILE *rs) {
  int size = BUFFERSIZE;
  char *last = malloc(BUFFERSIZE);

  if (sendP(ws, "\r\n.\r\n") == -1) {
    return -1;
  }
  fflush(ws);

  if (readP(&last, rs, &size) == -1) {
    return -1;
  }

  if (strncmp(last, "250", 3) != 0) {
    fprintf(stderr, "wrong code");
    return -1;
  }

  // send the QUIT message and check it
  if (sendP(ws, "QUIT") == -1) {
    return -1;
  }
  fflush(ws);

  if (readP(&last, rs, &size) == -1) {
    return -1;
  }
  if (fputs(last, stdout) == EOF) {
    perror("fputs 381");
    return -1;
  }

  if (strncmp(last, "221", 3) != 0) {
    fprintf(stderr, "wrong code");
    return -1;
  }

  free(last);
  return 0;
}

int main(int argc, char **argv) {
  char *subject = NULL;
  char *recipient;
  char Uadress[FQDN_LEN];
  char fqdn[FQDN_LEN];
  char name[FQDN_LEN];
  int rsock;
  int wsock;
  FILE *rs;
  FILE *ws;
  int opt;
  char *body;
  char *bodySend;

  while ((opt = getopt(argc, argv, "s:")) != -1) {
    switch (opt) {
    case 's':
      subject = optarg;
      break;
    default: // '?'
      fprintf(stderr, "Usage: snail [-s <subject>] address1");
      exit(EXIT_FAILURE);
    }
  }
  if (optind < argc) {
    recipient = argv[optind];
    optind++;
  }

  if (optind < argc || argc == 1) {
    fprintf(stderr, "Usage: snail [-s <subject>] address2");
    exit(EXIT_FAILURE);
  }

  if (getUserInfo(fqdn, Uadress, name) == CRIT_ERR) {
    exit(EXIT_FAILURE);
  }

  // get one connected socket
  if ((rsock = getConnSock()) < 0) {
    die("error while creating socket");
  }
  // get second connected socket by duplicating first one
  if ((wsock = dup(rsock)) == -1) {
    die("dup");
  }

  // open for read and write
  if ((rs = fdopen(rsock, "r")) == NULL || (ws = fdopen(wsock, "w")) == NULL) {
    die("fdopen");
  }
  if (checkSendHeader(fqdn, Uadress, recipient, name, ws, rs) == CRIT_ERR) {
    exit(EXIT_FAILURE);
  }

  // if subject is set, send it
  if (subject != NULL) {
    int len = strlen(subject) + strlen("Subject: ") + 3;
    char sub[len];
    if (snprintf(sub, len, "Subject: %s\r\n", subject) < 0) {
      perror("snprintf");
      exit(EXIT_FAILURE);
    }
    if (fputs(sub, stdout) == EOF) {
      perror("fputs 450");
      exit(EXIT_FAILURE);
    }
    if (sendP(ws, sub) == -1) {
      exit(EXIT_FAILURE);
    }
    fflush(stdout);
  }

  // send two newlines for start of body
  if (sendP(ws, "\r\n\r\n") == -1) {
    exit(1);
  }
  if (fputs("\r\n\r\n", stdout) == EOF) {
    perror("fputs 501");
    exit(EXIT_FAILURE);
  }
  fflush(stdout);

  if ((body = readBody()) == NULL) {
    exit(EXIT_FAILURE);
  }

  if ((bodySend = encode(body)) == NULL) {
    exit(EXIT_FAILURE);
  }

  if (sendP(ws, bodySend) == -1) {
    exit(EXIT_FAILURE);
  }

  free(bodySend);

  if (quit(ws, rs) == -1) {
    exit(EXIT_FAILURE);
  }

  fclose(rs);
  fclose(ws);
  close(rsock);
  close(wsock);
  return EXIT_SUCCESS;
}
