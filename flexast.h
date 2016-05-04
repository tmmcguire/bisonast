#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int yylex(void);

/*
 * Allocate memory "safely".
 */
static void *
astmalloc(int size)
{
  void *p = malloc(size);
  if (!p) { /* TODO: error */ }
  return p;
}

/*
 * Clone a string.
 */
static char *
clone_string(char *s, int l)
{
  char *clone = (char *) astmalloc(l + 1);
  if (!clone) { return 0; }
  memcpy(clone, s, l);
  clone[l] = '\0';
  return clone;
}

/* ------------------------------------ */

/* Initializing and deleting structures used to record token locations. */

struct position {
  /* Next stacked position */
  struct position *next;
  /* File name of position; "-" for strings */
  char *filename;
  /* Current line number in file */
  int cur_line;
  /* Current character number within line */
  int cur_col;
  /* Previous line number */
  int pre_line;
  /* Previous character number */
  int pre_col;
  /* Flex buffer state */
  YY_BUFFER_STATE buf;
  /* -- File -- */
  /* File to be scanned */
  FILE *file;
  int file_owned;
  /* -- String -- */
  /* String to be scanned */
  char *string;
};

/*
 * Set up a position.
 *
 * Don't call this function directly, use one of the specific versions below.
 */
static struct position *
set_pos_base(char *fn)
{
  struct position *p = (struct position *) astmalloc(sizeof(struct position));
  if (!p) { return 0; }
  if (fn) {
    /* duplicate the file name */
    int len = strlen(fn);
    p->filename = clone_string(fn, len);
    if (!p->filename) {
      free(p);
      return 0;
    }
  }
  p->next = 0;
  /* initialize the position */
  p->pre_line = p->cur_line = 1;
  p->pre_col = p->cur_col = 1;
  /* these will be dealt with below */
  p->file = 0;
  p->file_owned = 0;
  p->string = 0;
  return p;
}

/*
 * Grab a string and make it a flex buffer.
 */
static struct position *
set_pos_string(char *s, int s_len)
{
  struct position *p = set_pos_base("-");
  if (!p) { return 0; }
  /* duplicate the string as a flex buffer */
  p->string = (char *) astmalloc(s_len + 2);
  if (!p->string) {
    free(p->filename);
    free(p);
    return 0;
  }
  memcpy(p->string, s, s_len);
  /* the last two characters are special */
  p->string[s_len] = p->string[s_len + 1] = YY_END_OF_BUFFER_CHAR;
  /* point flex at the buffer; this makes it the current input */
  p->buf = yy_scan_buffer(p->string, s_len + 2);
  return p;
}

/*
 * Grab a file and point flex at it.
 *
 * This is for files opened outside the flex module and also used by the
 * function below for owned files.
 */
static struct position *
set_pos_file_unowned(char *fn, FILE *f)
{
  struct position *p = set_pos_base(fn);
  if (!p) { return 0; }
  /* remember the file */
  p->file = f;
  p->buf = yy_create_buffer(f, YY_BUF_SIZE);
  /* point flex at the file buffer */
  yy_switch_to_buffer(p->buf);
  return p;
}

/*
 * Open a file and point flex at it.
 *
 * This opens the file for input and marks it to be closed again automatically.
 */
static struct position *
set_pos_file_owned(char *fn)
{
  FILE *f = fopen(fn, "r");
  if (!f) {
    /* TODO: error */
    return 0;
  }
  struct position *p = set_pos_file_unowned(fn, f);
  if (!p) {
    fclose(f);
  }
  p->file_owned = 1;
  return 0;
}

/*
 * Clean up a position.
 */
 static void
 close_pos(struct position *p)
 {
   if (p->filename) {
     free(p->filename);
     p->filename = 0;
   }
   if (p->file && p->file_owned) {
     fclose(p->file);
     p->file = 0;
     p->file_owned = 0;
   }
   if (p->string) {
     free(p->string);
     p->string = 0;
   }
   if (p->buf) {
     yy_delete_buffer(p->buf);
     p->buf = 0;
   }
 }

/* ------------------------------------ */

/* Manipulating positions within a buffer */

static void
advance_pos(struct position *p, char *text, int len)
{
  /* record the previous location */
  p->pre_line = p->cur_line;
  p->pre_col = p->cur_col;
  /* advance the current location */
  int i;
  for (i = 0; i < len; ++i) {
    if (text[i] == '\n') {
      ++p->cur_line;
      p->cur_col = 1;
    } else {
      ++p->cur_col;
    }
  }
}

/*
 * Macros for use in flex specification rules, for whitespace, etc. Returned
 * tokens already advance the position.
 *
 * ADVANCE is probably the easiest to use; it looks at the scanner's yytext and
 * yyleng. ADVANCE2 needs the text and its length.
 */
#define ADVANCE2(t,l) advance_pos(scanner.pstack, (t), (l))
#define ADVANCE ADVANCE2(yytext, yyleng)

/* ------------------------------------ */

/* The information needed by the scanner, overall. */

struct scanner {
  /* the current positions */
  struct position *pstack;
  /* value of the last call to yylex */
  int last_token;
};

/* This structure is static and associated with the file for this scanner. */
static struct scanner scanner = { 0, 0, };

/*
 * Check if we are currently scanning something.
 */
static int
is_scanning(void)
{
  return scanner.pstack != 0;
}

/*
 * Push a file onto the position stack.
 *
 * Return true if successful, false otherwise.
 */
static int
push_position(char *fn)
{
  /* create position structure and open the file */
  struct position *p = set_pos_file_owned(fn);
  if (!p) { return 0; }
  /* put it on top of the stack */
  p->next = scanner.pstack;
  scanner.pstack = p;
  return 1;
}

/*
 * Push a file onto the position stack, where the filename comes from a string
 * buffer.
 *
 * Return true if successful, false otherwise.
 */
static int
push_position2(char *begin, int len)
{
  int res = 0;
  char *buf = (char *) astmalloc(len + 1);
  if (buf) {
    memcpy(buf, begin, len);
    buf[len] = 0;
    res = push_position(buf);
    free(buf);
  }
  return res;
}

/*
 * Macros for use in Flex rules.
 *
 * PUSH_FILE_YYTEXT pushes a file onto the position stack; it takes two
 * arguments, the index into the yytext buffer of the beginning of the file name
 * and the index of the next position after the file name.
 *
 * PUSH_FILE_STRING pushes a file onto the position stack, where the file name
 * is given by the string. PUSH_FILE_STRING2 does the same, but needs the length
 * of the filename.
 */
#define PUSH_FILE_YYTEXT(b,e) (ADVANCE, push_position2(yytext+(b), (e)-(b)))
#define PUSH_FILE_STRING(s) push_position((s))
#define PUSH_FILE_STRING2(s,l) (push_position2((s), (l)))

/* ------------------------------------ */

/*
 * Pop the top of the position stack.
 *
 * THIS IS A MAGIC FUNCTION.
 *
 * yywrap is called when there is an end-of-file; this should transfer back to
 * previously pushed positions if there are any. Otherwise, it returns 1 to tell
 * flex that we have reached the end of the input.
 */
static int
yywrap(void)
{
  /* close and free the top of the stack */
  struct position *p = scanner.pstack;
  scanner.pstack = p->next;
  close_pos(p);
  free(p);
  if (!scanner.pstack) {
    /* if that was the last position, quit */
    return 1;
  } else {
    /* switch back to the previous buffer */
    yy_switch_to_buffer(scanner.pstack->buf);
    return 0;
  }
}

/* ------------------------------------ */

/* Scanning functions */

/*
 * Begin scanning a string.
 *
 * Returns true on success, false otherwise.
 *
 * Parameters:
 * - A string to scan.
 */
static int
on_string(char *string, int length)
{
  if (is_scanning()) {
    /* already scanning */
    return 0;
  }
  scanner.pstack = set_pos_string(string, length);
  if (!scanner.pstack) {
    return 0;
  }
  return 1;
}

/*
 * Begin scanning a file based on a file name.
 *
 * Returns true on success, false otherwise.
 *
 * Parameters:
 * - A file name string to open and scan.
 */
static int
on_file_name(char *filename)
{
  if (is_scanning()) {
    /* already scanning */
    return 0;
  }
  scanner.pstack = set_pos_file_owned(filename);
  if (!scanner.pstack) {
    return 0;
  }
  return 1;
}

/*
 * Begin scanning a file based on a FILE pointer.
 *
 * Returns true on success, false otherwise.
 *
 * Parameters:
 * - A file name, for position recording.
 * - A FILE pointer open for reading.
 */
static int
on_file_pointer(char *filename, FILE *file)
{
  if (is_scanning()) {
    /* already scanning */
    return 0;
  }
  scanner.pstack = set_pos_file_unowned(filename, file);
  if (!scanner.pstack) {
    return 0;
  }
  return 1;
}

/*
 * Close down the scanner, freeing any allocated space.
 *
 * Returns true on success, false otherwise.
 */
static int
close_scanner()
{
  if (!is_scanning()) {
    return 1;
  }
  while (scanner.pstack) {
    /* clean out the position stack */
    struct position *next = scanner.pstack->next;
    close_pos(scanner.pstack);
    free(scanner.pstack);
    scanner.pstack = next;
  }
  scanner.last_token = 0;
  return 1;
}

/* ------------------------------------ */

struct location {
  int line;
  int col;
};

void
init_location(struct location *l, char *filename, int line, int col)
{
  l->line = line;
  l->col = col;
}

struct range {
  struct range *next;
  char *filename;
  struct location start;
  struct location end;
}

struct range *
new_range(char *filename, int start_line, int start_col, int end_line,
  int end_col, struct range *next)
{
  struct range *r = (struct range *) astmalloc(sizeof(struct range));
  if (!r) { return 0; }
  r->filename = clone_string(filename, strlen(filename));
  if (!r->filename) {
    free_range(next);
    free(r);
    return 0;
  }
  init_location(&(r->start), filename, start_line, start_col);
  init_location(&(r->end), filename, end_line, end_col);
  return 1;
}

void
free_range(struct range *r)
{
  while (r) {
    struct range *next = r->next;
    if (r->filename) {
      free(r->filename);
    }
    r = next;
  }
}

struct range *
range_of_position(struct position *p)
{
  if (p) {
    struct range *r_next = range_of_position(p->next);
    if (!r_next) { return 0; }
    struct range *r = new_range(p->filename, p->pre_line, p->pre_col,
      p->cur_line, p->cur_col-1, r_next);
    if (!r) {
      free_range(r_next);
      return 0;
    }
    return r;
  } else {
    return 0;
  }
}

struct token {
  struct range *location;
  int token;
  char *text;
};

struct token *
make_token()
{
  struct token *t = (struct token *) astmalloc(sizeof(struct token));
  if (!t) { return 0; }
  struct range *p = range_of_position(scanner.pstack);
  if (!p) {
    free(t);
    return 0;
  }
  t->location = p;
  t->token = scanner.lasttoken;
  t->text = clone_string(yytext, yyleng);
  return t;
}

struct token *
read_token(void)
{
  if (!is_scanning()) {
    /* TODO: error */
    return 0;
  }
  scanner.lasttoken = yylex();
  if (!scanner.lasttoken) {
    /* out of tokens */
    return 0;
  }
  ADVANCE;
  return make_token();
}

struct token *
last_token()
{
  if (!is_scanning() || !scanner.lasttoken) {
    /* TODO: error */
    return 0;
  }
  return make_token();
}

/* maketokens */
