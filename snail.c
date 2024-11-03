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

/**
 * PS: Hier und an allen Funktionen fehlt das static!!!
 * Die Funktionen und Variablen sind also nach außen sichtbar.
 * In SP bekommst du dafür Punkte abgezogen.
 *
 * Die Variable hier könntest du außerdem durch ein #define ersetzen.
 */
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
/**
 * PS: Zu @Korrektur:
 * Ich finde es meistens sinnvoll, Speicher innerhalb von Funktionen zu allokieren.
 * Dann ist man flexibel den Speicherbereich auch im Nachhinein zu erweitern. Was dann beachtet werden sollte:
 * - Der Speicher muss außerhalb der Funktion wieder freigegeben werden.
 * - Im Fehlerfall, sollte der Speicher in der Funktion selbst wieder freigegeben werden (weiß aber nicht ob wir das in SP machen _müssen_).
 * In beiden Fällen sollte klar geregelt sein, wie zu verfahren ist. Das einheitlich zu gestalten führt zu besserem Code.
 */
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
 /**
  * PS: Der Einfachheit halber würde ich empfehlen den Speicher vollständig innerhalb der Funktion zu allokieren und nicht teilweise außerhalb.
  * Dann hätte dir der Fehler weiter unten in dieser Funktion auch nicht passieren können.
  */
// takes allocated buffer, reads in a line from filepointer and returns position
// of last char: \0
int readP(char **buffer, FILE *rs, int *size) {
  char c;
  int pos = 0;
  char *line = *buffer;

  if (!line) {
    return -1;
  }

  // on error return EOF or if sock is closed
  /**
   * PS: ACHTUNG: Dieser Check wird niemals false. Hier passiert etwas sehr gemeines.
   * Der Zuweisungsoperator in C ist so definiert:
   * 1. Der R-Wert (Wert rechts des =) wird in falls nötig in den Typ des L-Werts (Variable) umgewandelt. Hier int -> char.
   * 2. Der R-Wert wird dem L-Wert (der Variablen) zugewiesen.
   * 3. Der Zuweisungsoperator liefert das Ergebnis des R-Werts (hier (char) fgetc(rs)) zurück.
   *
   * Das Problem hier ist, char ein 8 bit Datentyp ist und int meist! ein 32 bit Datentyp.
   * EOF == -1 entspricht im Zweierkomplement der 32 bit Darstellung 0xffffffff.
   * Selbst wenn EOF (-1) zurückgegeben wird, wird das durch den Cast zu char zu einem 8 bit Datentyp -> 0xff.
   *
   * Der Compiler könnte das bereits vorhersehen.
   * Aber weil wir hier in C sind macht er das natürlich nicht, sondern setzt alles genauso um wie es dasteht mit allen Konsequenzen.
   * In Java würde diese Zuweisung übrigens einen expliziten Cast erfordern.
   */
  while ((c = fgetc(rs)) != EOF) {
    if (pos >= *size - 1) {
      *size += BUFFERSIZE;
	  /**
	   * PS: ACHTUNG: Das hier kann zu einem riesen Problem führen!
	   * Sobald dieses realloc() einmal ausgeführt wurde ist der Speicher von außen über buffer nicht mehr erreichbar.
	   * Schlimmer noch: Ein Zugriff auf *buffer von außen ist sogar ungültig.
	   *
	   * realloc() vergrößert den Speicher und schreibt den neuen pointer zum Stringanfang in line.
	   * In *buffer steht aber immer noch der Pointer zum alten Stringanfang!
	   * Dieser wurde gerade durch realloc() aber freigegeben.
	   *
	   * Das hier kann nur durch Zufall funktionieren, wenn:
	   * a) realloc() nie aufgerufen wird
	   * b) für denn vergrößerten Speicher zufällig der gleiche Speicherort wie für *buffer verwenden wird (nach MAN page explizit möglich).
	   *
	   * Insgesamt ist das hier aber undefined behaviour.
	   */
      if ((line = realloc(line, *size)) == NULL) {
        perror("realloc");
        return -1;
      }
    }

    line[pos++] = c;
    if (c == '\n') {
      break;
    }
  }
  if (ferror(rs)) {
    perror("fgetc");
    return -1;
  }

  line[pos] = '\0'; // null terminate string
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
  /**
   * PS: Interessant. Wusste nicht, dass mann snprintf() dafür benutzen kann :)
   * Aber find ich gut.
   */
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
  /**
   * PS: ACHTUNG info[4] wird nicht überprüft!!
   * da 4 < 4 == false
   */
  for (int i = 0; i < 4; i++) {
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

  /**
   * PS: ACHTUNG:
   * Hier wird auch header[4] nicht freigegeben, da 4 < 4 == false.
   */
  for (int i = 0; i < 4; i++) {
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
 * Write email body to the server, ensuring SMTP compliance
 * @param ws: Write stream to the server
 * @param rs: Read stream from the server
 * @return: -1 if error occurs, 0 on success
 */
int writeTo(FILE *ws, FILE *rs) {
  char c;       // Current character being read
  char next;    // Next character after a period to check for the end-of-message
                // marker
  char *answer; // Buffer to store the server’s response
  int size;     // Size of the server's response

  // Loop to read and write each character in the message
  /**
   * PS: Die Ifs in der Schleife sind sehr undurchsichtig und kompliziert. Hier können leicht Fehler passieren.
   * Hier passiert sehr viel auf einmal. Das würde ich etwas aufteilen:
   * 1. Die gesamte Nachricht vom stdin bis EOF einlesen und in einen String speichern (logischerweise dynamisch allokiert).
   * 2. Eine encoding-Funktion schreiben, die Punkte am Zeilenanfang escaped und sicherstellt, dass die Zeilenumbrüche alle \r\n sind.
   *    Dafür muss natürlich ein neuer String angelegt werden.
   * 3. Anschließend kannst du ganz einfach den kodierten String mit fputs() oder ähnlichem versenden.
   *
   * Das würde den Code hier etwas entzerren und weniger fehleranfällig machen.
   */
  while ((c = fgetc(stdin)) != EOF) {
    // Check if the current character is a period ('.')
    if (c == '.') {
      // Read the next character after the period
      if ((next = fgetc(stdin)) == EOF) {
        // Handle any read error
        if (ferror(stdin)) {
          perror("fgetc");
          return -1;
        }
      }

      // If the next character is not '\r' or '\n'
      if (next != '\r' && next != '\n') {
        // Write two periods to "escape" the single period in the message
        if ((fputc('.', ws) == EOF) || (fputc('.', ws) == EOF)) {
          if (ferror(ws)) { // Check for any write errors
            perror("fputc");
            return -1;
          }
        }
        ungetc(next, stdin); // Put the character back in the input stream

      }
      // If the next character is '\r' or '\n', it indicates the end of the
      // message
	  /**
	   * PS: ACHTUNG: Die Aufgabenstellung sagt nicht, das der Nutzer mit "\r\n.\r\n" die Nachricht beenden soll.
	   * Der Nutzer soll lediglich Text über die Standardeingabe eingeben und den Text mit CTRL+D (EOF) beenden.
	   * CTRL+D sorgt dafür, dass im Stream EOF gelesen wird, wodurch das Lesen abbricht.
	   *
	   * Selbst wenn der benutzer \r\n.\r\n eingeben würde, müsste das Programm den Punkt durch einen zweiten escapen
	   * -> \r\n..\r\n
	   *
	   * Dieser Check hier ist also falsch.
	   *
	   * Ganz davon abgesehen, ist die If-Bedingung hier immer true.
	   */
      else if (next == '\r' || next == '\n') {
        fprintf(stderr, "test");
        // Write the period and line break to signal the end of the message
        if (fputs(".\r\n", ws) == EOF) {
          if (ferror(ws)) { // Check for any write errors
            perror("fputs 318");
            return -1;
          }
          break; // Exit the loop as the end of the message is reached
        }
      }
    }
    // If the current character is a simple newline '\n'
	/**
	 * PS: Auch hier muss eigentlich das nächste Zeichen überprüft werden.
	 * Was passiert, wenn der Benutzer wirklich \r\n eingibt und nicht nur \n?
	 * -> Du ersetzt dann \r\n durch \r\r\n, was den String verändert.
	 */
    else if (c == '\n') {
      // Write "\r\n" for SMTP-compliant line breaks
      if (fputs("\r\n", ws) == EOF) {
        if (ferror(ws)) { // Check for any write errors
          perror("fputs 327");
          return -1;
        }
      }
    }
    // All other characters are written directly
    else {
      if (fputc(c, ws) == EOF) {
        if (ferror(ws)) { // Check for any write errors
          perror("fputc 334");
          return -1;
        }
      }
    }
  }

  // Check if any errors occurred while reading from stdin
  if (ferror(stdin)) {
    perror("fgetc");
    return -1;
  }

  // Flush the output stream to ensure data is sent
  fflush(ws);

  // Allocate memory for the server response
  if ((answer = calloc(BUFFERSIZE, sizeof(char))) == NULL) {
    perror("calloc");
    return -1;
  }
  // Read the server’s response
  if (readP(&answer, rs, &size) == -1) {
    free(answer);
    return -1;
  }

  // Verify if the server's response contains the "250" status code
  if (strncmp(answer, "250", 3) != 0) {
    fprintf(stderr, "wrong code"); // Print an error if the code is not 250
    free(answer);
    return -1;
  }

  free(answer); // Free the allocated memory for the response
  return 0;     // Successful completion of the function
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
    fprintf(stderr, "wroing code");
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

  if (writeTo(ws, rs) == -1) {
    exit(EXIT_FAILURE);
  }

  if (quit(ws, rs) == -1) {
    exit(EXIT_FAILURE);
  }

  fclose(rs);
  fclose(ws);
  close(rsock);
  close(wsock);
  return EXIT_SUCCESS;
}
