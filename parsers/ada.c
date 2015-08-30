/* File:          Ada.c
 * Description:   Enables extended Ada parsing support in Exuberant Ctags
 * Version:       0.6
 * Date:          October 26, 2006
 * Author:        A. Aaron Cornelius (ADotAaronDotCorneliusAtgmailDotcom)
 *
 * Installation:
 * You must have the Exuberant Ctags source to install this parser.  Once you
 * have the source, place this file into the directory with the rest of the
 * ctags source.  After ada.c is in the correct directory you need to make the
 * following changes so that the ada parser is included when you compile and
 * install ctags:
 *
 * to file source.mak add the line
 *   ada.c \
 *
 * after
 * SOURCES = \
 *
 * then add the line
 *   ada.$(OBJECT) \
 *
 * after
 * OBJECTS = \
 *
 * to file parsers.h add the line
 *     AdaParser, \
 *
 * after
 * #define PARSER_LIST \
 *
 * Then compile and install ctags as normal (usually: './configure', './make',
 * './make install').
 *
 * Changelog:
 *
 * 11/02/2006 - Completed implementation of file scope info and qualified tags
 *              information gathering.
 * 11/02/2006 - Added recognition of private flag in a token for file scope
 *              checking purposes.
 * 10/27/2006 - Added full package scope name when --extra=+q is set.
 * 10/27/2006 - Fixed file scope setting, and added check to verify that tags
 *              with file scope should be included in the tag file.
 * 10/26/2006 - Fixed error which caused infinite loop when parsing some
 *              files.
 * 0.5 - Bugfixes
 * 10/20/2006 - Cleaned up freeAdaTokenList.
 * 10/20/2006 - Fixed error in freeAdaToken that caused the child token lists
 *              to become corrupted when "separate" tokens were deleted.
 * 0.4 - Third Revision - 09/25/2006
 * 09/25/2006 - Fixed error in newAdaToken which could cause an error on some
 *              systems when a separate token (which is temporary) gets
 *              created.
 * 09/25/2006 - Change matchFilePos initialization in the findAdaTags
 *              function.
 * 0.3 - Second Revision
 * 06/02/2006 - Added missing EOF checks to prevent infinite loops in the case
 *              of an incomplete Ada (or non-Ada) file being parsed.
 * 06/02/2006 - Added Copyright notice.
 * 0.2 - First Revision
 * 05/26/2006 - Fixed an error where tagging the proper scope of something
 *              declared in an anonymous block or anonymous loop was not
 *              working properly.
 * 05/26/2006 - Fixed an error capturing the name of a 'separate' tag.
 * 05/26/2006 - Fixed the cmp() function so that it finds matches correctly.
 * 05/26/2006 - Fixed some spelling errors.
 * 05/26/2006 - Added explicit skipping of use and with clauses.
 * 0.1 - Initial Release
 *
 * Future Changes:
 * TODO: Add inheritance information?
 * TODO: Add signature gathering?
 *
 * Copyright (C) 2006 A. Aaron Cornelius
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
 * USA.
 *
 */

#include "general.h"    /* always include first */

#include <string.h>     /* to declare strxxx() functions */
#include <ctype.h>      /* to define isxxx() macros */
#include <setjmp.h>

#include "parse.h"      /* always include */
#include "read.h"       /* to define file fileReadLine() */
#include "entry.h"      /* for the tag entry manipulation */
#include "routines.h"   /* for generic malloc/realloc/free routines */
#include "options.h"    /* for the Option structure */
#include "debug.h"      /* for Assert */

typedef enum eAdaException
{
  EXCEPTION_NONE,
  EXCEPTION_EOF
} adaException;

static adaException exception;

typedef enum eAdaParseMode
{
  ADA_ROOT,
  ADA_DECLARATIONS,
  ADA_CODE,
  ADA_EXCEPTIONS,
  ADA_GENERIC
} adaParseMode;

typedef enum eAdaKinds
{
  ADA_KIND_SEPARATE = -2,   /* for defining the parent token name of a child
                             * sub-unit */
  ADA_KIND_UNDEFINED = -1,  /* for default/initialization values */
  ADA_KIND_PACKAGE_SPEC,
  ADA_KIND_PACKAGE,
  ADA_KIND_TYPE_SPEC,
  ADA_KIND_TYPE,
  ADA_KIND_SUBTYPE_SPEC,
  ADA_KIND_SUBTYPE,
  ADA_KIND_RECORD_COMPONENT,
  ADA_KIND_ENUM_LITERAL,
  ADA_KIND_VARIABLE_SPEC,
  ADA_KIND_VARIABLE,
  ADA_KIND_FORMAL,
  ADA_KIND_CONSTANT,
  ADA_KIND_EXCEPTION,
  ADA_KIND_SUBPROGRAM_SPEC,
  ADA_KIND_SUBPROGRAM,
  ADA_KIND_TASK_SPEC,
  ADA_KIND_TASK,
  ADA_KIND_PROTECTED_SPEC,
  ADA_KIND_PROTECTED,
  ADA_KIND_ENTRY_SPEC,
  ADA_KIND_ENTRY,
  ADA_KIND_LABEL,
  ADA_KIND_IDENTIFIER,
  ADA_KIND_AUTOMATIC_VARIABLE,
  ADA_KIND_ANONYMOUS,      /* for non-identified loops and blocks */
  ADA_KIND_COUNT            /* must be last */
} adaKind;

static kindOption AdaKinds[] =
{
  { TRUE,  'P', "packspec",    "package specifications" },
  { TRUE,   'p', "package",     "packages" },
  { FALSE,  'T', "typespec",    "type specifications" },
  { TRUE,   't', "type",        "types" },
  { FALSE,  'U', "subspec",     "subtype specifications" },
  { TRUE,   'u', "subtype",     "subtypes" },
  { TRUE,   'c', "component",   "record type components" },
  { TRUE,   'l', "literal",     "enum type literals" },
  { FALSE,  'V', "varspec",     "variable specifications" },
  { TRUE,   'v', "variable",    "variables" },
  { TRUE,   'f', "formal",      "generic formal parameters" },
  { TRUE,   'n', "constant",    "constants" },
  { TRUE,   'x', "exception",   "user defined exceptions" },
  { TRUE,  'R', "subprogspec", "subprogram specifications" },
  { TRUE,   'r', "subprogram",  "subprograms" },
  { TRUE,  'K', "taskspec",    "task specifications" },
  { TRUE,   'k', "task",        "tasks" },
  { TRUE,  'O', "protectspec", "protected data specifications" },
  { TRUE,   'o', "protected",   "protected data" },
  { FALSE,  'E', "entryspec",   "task/protected data entry specifications" },
  { TRUE,   'e', "entry",       "task/protected data entries" },
  { TRUE,   'b', "label",       "labels" },
  { TRUE,   'i', "identifier",  "loop/declare identifiers"},
  { FALSE,  'a', "autovar",     "automatic variables" },
  { FALSE,  'y', "annon",       "loops and blocks with no identifier" }
};

typedef struct sAdaTokenList
{
  int numTokens;
  struct sAdaTokenInfo *head;
  struct sAdaTokenInfo *tail;
} adaTokenList;

typedef struct sAdaTokenInfo
{
  adaKind kind;
  boolean isSpec;
  boolean isPrivate;
  char *name;
  tagEntryInfo tag;
  struct sAdaTokenInfo *parent;
  struct sAdaTokenInfo *prev;
  struct sAdaTokenInfo *next;
  adaTokenList children;
} adaTokenInfo;

typedef enum eAdaKeywords
{
  ADA_KEYWORD_ACCEPT,
  ADA_KEYWORD_BEGIN,
  ADA_KEYWORD_BODY,
  ADA_KEYWORD_CASE,
  ADA_KEYWORD_CONSTANT,
  ADA_KEYWORD_DECLARE,
  ADA_KEYWORD_DO,
  ADA_KEYWORD_ELSE,
  ADA_KEYWORD_ELSIF,
  ADA_KEYWORD_END,
  ADA_KEYWORD_ENTRY,
  ADA_KEYWORD_EXCEPTION,
  ADA_KEYWORD_FOR,
  ADA_KEYWORD_FUNCTION,
  ADA_KEYWORD_GENERIC,
  ADA_KEYWORD_IF,
  ADA_KEYWORD_IN,
  ADA_KEYWORD_IS,
  ADA_KEYWORD_LOOP,
  ADA_KEYWORD_NEW,
  ADA_KEYWORD_OR,
  ADA_KEYWORD_PACKAGE,
  ADA_KEYWORD_PRAGMA,
  ADA_KEYWORD_PRIVATE,
  ADA_KEYWORD_PROCEDURE,
  ADA_KEYWORD_PROTECTED,
  ADA_KEYWORD_RECORD,
  ADA_KEYWORD_RENAMES,
  ADA_KEYWORD_SELECT,
  ADA_KEYWORD_SEPARATE,
  ADA_KEYWORD_SUBTYPE,
  ADA_KEYWORD_TASK,
  ADA_KEYWORD_THEN,
  ADA_KEYWORD_TYPE,
  ADA_KEYWORD_UNTIL,
  ADA_KEYWORD_USE,
  ADA_KEYWORD_WHEN,
  ADA_KEYWORD_WHILE,
  ADA_KEYWORD_WITH
} adaKeyword;

static const char *AdaKeywords[] =
{
  "accept",
  "begin",
  "body",
  "case",
  "constant",
  "declare",
  "do",
  "else",
  "elsif",
  "end",
  "entry",
  "exception",
  "for",
  "function",
  "generic",
  "if",
  "in",
  "is",
  "loop",
  "new",
  "or",
  "package",
  "pragma",
  "private",
  "procedure",
  "protected",
  "record",
  "renames",
  "select",
  "separate",
  "subtype",
  "task",
  "then",
  "type",
  "until",
  "use",
  "when",
  "while",
  "with"
};

/* a jump buffer for fail-safe error prevention */
static jmp_buf eofError;

/* a simple var to keep track of how many times we hit EOF... If we hit it
 * say, about 1000 times, we will print an error, jump to the end of the
 * program, and store what tags we have */
static int eofCount;

/* variables for managing the input string, position as well as input line
 * number and position */
static const char *line;
static int lineLen;
static int pos;
static unsigned long matchLineNum;
static fpos_t matchFilePos;

/* utility functions */
static void makeSpec(adaKind *kind);

/* prototypes of functions for manipulating the Ada tokens */
static adaTokenInfo *newAdaToken(const char *name, int len,
                                 adaKind kind, boolean isSpec,
                                 adaTokenInfo *parent);
static void freeAdaToken(adaTokenList *list, adaTokenInfo *token);
static void appendAdaToken(adaTokenInfo *parent, adaTokenInfo *token);

/* token list processing function prototypes */
static void initAdaTokenList(adaTokenList *list);
static void freeAdaTokenList(adaTokenList *list);
static void appendAdaTokenList(adaTokenInfo *parent, adaTokenList *children);

/* prototypes of functions for moving through the DEFINED text */
static void readNewLine(void);
static void movePos(int amount);
static boolean cmp(const char *buf, int len, const char *match);
static boolean adaCmp(const char *match);
static boolean adaKeywordCmp(adaKeyword keyword);
static void skipUntilWhiteSpace(void);
static void skipWhiteSpace(void);
static void skipPast(const char *past);
static void skipPastKeyword(adaKeyword keyword);
static void skipPastWord(void);

/* prototypes of functions for parsing the high-level Ada constructs */
static adaTokenInfo *adaParseBlock(adaTokenInfo *parent, adaKind kind);
static adaTokenInfo *adaParseSubprogram(adaTokenInfo *parent, adaKind kind);
static adaTokenInfo *adaParseType(adaTokenInfo *parent, adaKind kind);
static adaTokenInfo *adaParseVariables(adaTokenInfo *parent, adaKind kind);
static adaTokenInfo *adaParseLoopVar(adaTokenInfo *parent);
static adaTokenInfo *adaParse(adaParseMode mode, adaTokenInfo *parent);

/* prototypes of the functions used by ctags */
static void storeAdaTags(adaTokenInfo *token, const char *parentScope);
static void findAdaTags(void);
extern parserDefinition* AdaParser(void);

static void makeSpec(adaKind *kind)
{
  switch(*kind)
  {
    case ADA_KIND_PACKAGE:
      *kind = ADA_KIND_PACKAGE_SPEC;
      break;

    case ADA_KIND_TYPE:
      *kind = ADA_KIND_TYPE_SPEC;
      break;

    case ADA_KIND_SUBTYPE:
      *kind = ADA_KIND_SUBTYPE_SPEC;
      break;

    case ADA_KIND_VARIABLE:
      *kind = ADA_KIND_VARIABLE_SPEC;
      break;

    case ADA_KIND_SUBPROGRAM:
      *kind = ADA_KIND_SUBPROGRAM_SPEC;
      break;

    case ADA_KIND_TASK:
      *kind = ADA_KIND_TASK_SPEC;
      break;

    case ADA_KIND_PROTECTED:
      *kind = ADA_KIND_PROTECTED_SPEC;
      break;

    case ADA_KIND_ENTRY:
      *kind = ADA_KIND_ENTRY_SPEC;
      break;

    default:
      *kind = ADA_KIND_UNDEFINED;
      break;
  }
} /* static void makeSpec(adaKind *kind) */

static adaTokenInfo *newAdaToken(const char *name, int len, adaKind kind,
                                 boolean isSpec, adaTokenInfo *parent)
{
  char *tmpName = NULL;
  adaTokenInfo *token = xMalloc(1, adaTokenInfo);

  token->name = NULL;

  if(name != NULL && len != 0)
  {
    tmpName = xMalloc(len + 1, char);
    strncpy(tmpName, name, len);
    tmpName[len] = '\0';
  }

  /* init the tag */
  initTagEntry(&token->tag, tmpName);

  token->kind = kind;
  token->isSpec = isSpec;
  token->isPrivate = FALSE;

  /* set the token data */
  token->name = tmpName;
  token->parent = parent;

  /* Now set the file scope for this tag.  A tag has file scope if its direct 
   * parent is a package/subprogram/protected/task spec, or if it it's parent 
   * is UNDEFINED (a 'root' token), and if this is not in a 'private' section 
   * of that spec. */
  if((parent != NULL) && (parent->isPrivate == FALSE) &&
     ((parent->kind == ADA_KIND_UNDEFINED) ||
      (parent->kind == ADA_KIND_SEPARATE) ||
      ((parent->isSpec == TRUE) && ((parent->kind == ADA_KIND_PACKAGE) ||
                                    (parent->kind == ADA_KIND_SUBPROGRAM) ||
                                    (parent->kind == ADA_KIND_PROTECTED) ||
                                    (parent->kind == ADA_KIND_TASK)))))
  {
    token->tag.isFileScope = FALSE;
  }
  else
  {
    token->tag.isFileScope = TRUE;
  }

  /* add the kind info - unless this is a SEPARATE kind, in which case keep 
   * them blank because they get filled in later. */
  if(kind > ADA_KIND_UNDEFINED)
  {
    token->tag.kind = &(AdaKinds[kind]);
  }
  else
  {
    token->tag.kind = NULL;
  }

  /* setup the parent and children pointers */
  initAdaTokenList(&token->children);
  appendAdaToken(parent, token);

  return token;
} /* static adaTokenInfo *newAdaToken(const char *name, int len, ... ) */

static void freeAdaToken(adaTokenList *list, adaTokenInfo *token)
{
  if(token != NULL)
  {
    if(token->name != NULL)
    {
      eFree((void *) token->name);
      token->name = NULL;
    }

    /* before we delete this token, clean up it's children */
    freeAdaTokenList(&token->children);

    /* move the next token in the list to this token's spot */
    if(token->prev != NULL)
    {
      token->prev->next = token->next;
    }
    else if(list != NULL && token->prev == NULL)
    {
      list->head = token->next;
    }

    /* move the previous token in the list to this token's spot */
    if(token->next != NULL)
    {
      token->next->prev = token->prev;
    }
    else if(list != NULL && token->next == NULL)
    {
      list->tail = token->prev;
    }

    /* decrement the list count */
    if(list != NULL)
    {
      list->numTokens--;
    }

    /* now that this node has had everything hanging off of it rearranged,
     * delete this node */
    eFree(token);
  } /* if(token != NULL) */
} /* static void freeAdaToken(adaTokenList *list, adaTokenInfo *token) */

static void appendAdaToken(adaTokenInfo *parent, adaTokenInfo *token)
{
  /* if the parent or newChild is NULL there is nothing to be done */
  if(parent != NULL && token != NULL)
  {
    /* we just need to add this to the list and set a parent pointer */
    parent->children.numTokens++;
    token->parent = parent;
    token->prev = parent->children.tail;
    token->next = NULL;

    if(parent->children.tail != NULL)
    {
      parent->children.tail->next = token;
    }

    /* the token that was just added always becomes the last token int the
     * list */
    parent->children.tail = token;

    if(parent->children.head == NULL)
    {
      parent->children.head = token;
    }
  } /* if(parent != NULL && token != NULL) */
} /* static void appendAdaToken(adaTokenInfo *parent, adaTokenInfo *token) */

static void initAdaTokenList(adaTokenList *list)
{
  if(list != NULL)
  {
    list->numTokens = 0;
    list->head = NULL;
    list->tail = NULL;
  }
}

static void freeAdaTokenList(adaTokenList *list)
{
  if(list != NULL)
  {
    while(list->head != NULL)
    {
      freeAdaToken(list, list->head);
    }
  }
}

static void appendAdaTokenList(adaTokenInfo *parent, adaTokenList *children)
{
  adaTokenInfo *tmp = NULL;

  if(parent != NULL && children != NULL)
  {
    while(children->head != NULL)
    {
      tmp = children->head->next;
      appendAdaToken(parent, children->head);

      /* we just need to worry about setting the head pointer properly during
       * the list iteration.  The node's pointers will get set properly by the
       * appendAdaToken() function */
      children->head = tmp;
    }

    /* now that we have added all nodes from the children list to the parent
     * node, zero out the children list */
    initAdaTokenList(children);
  }
} /* static void appendAdaTokenList(adaTokenInfo *parent, ... ) */

static void readNewLine(void)
{
  while(TRUE)
  {
    line = (const char *) fileReadLine();
    pos = 0;

    if(line == NULL)
    {
      lineLen = 0;
      exception = EXCEPTION_EOF;
      eofCount++;

      if(eofCount >= 1000)
      {
        longjmp(eofError, exception);
      }
      else
      {
        return;
      }
    } /* if(line == NULL) */

    lineLen = strlen(line);

    if(lineLen > 0)
    {
      return;
    }
  } /* while(TRUE) */
} /* static void readNewLine(void) */

static void movePos(int amount)
{
  pos += amount;
  if(exception != EXCEPTION_EOF && pos >= lineLen)
  {
    readNewLine();
  }
}

/* a macro for checking for comments... This isn't the same as the check in
 * cmp() because comments don't have to have whitespace or separation-type
 * characters following the "--" */
#define isAdaComment(buf, pos, len) \
  (((pos) == 0 || (!isalnum((buf)[(pos) - 1]) && (buf)[(pos) - 1] != '_')) && \
   (pos) < (len) && \
   strncasecmp(&(buf)[(pos)], "--", strlen("--")) == 0)

static boolean cmp(const char *buf, int len, const char *match)
{
  boolean status = FALSE;
  int matchLen;

  /* if we are trying to match nothing, that is always true */
  if(match == NULL)
  {
    return TRUE;
  }

  /* first check to see if the buffer is empty, if it is, return false */
  if(buf == NULL)
  {
    return status;
  }

  matchLen = strlen(match);

  /* A match only happens the number of chars in the matching string match,
   * and whitespace follows... Which means we also must check to see if the
   * end of the line is after the matching string.  Also check for some
   * separation characters such as (, ), :, or ; */
  if ((strncasecmp(buf, match, matchLen) == 0) &&
	  (matchLen == len ||
	  (matchLen < len &&
	   (isspace(buf[matchLen]) || buf[matchLen] == '(' ||
	    buf[matchLen] == ')' || buf[matchLen] == ':' ||
	    buf[matchLen] == ';'))))
  {
    status = TRUE;
  }

  return status;
} /* static boolean cmp(char *buf, int len, char *match) */

static boolean adaCmp(const char *match)
{
  boolean status = FALSE;

  /* first check to see if line is empty, if it is, throw an exception */
  if(line == NULL)
  {
    exception = EXCEPTION_EOF;
    return status;
  }

  status = cmp(&line[pos], lineLen - pos, match);

  /* if we match, increment the position pointer */
  if(status == TRUE && match != NULL)
  {
    matchLineNum = getSourceLineNumber();
    matchFilePos = getInputFilePosition();

    movePos((strlen(match)));
  }

  return status;
} /* static boolean adaCmp(char *match) */

/* just a version of adaCmp that is a bit more optimized for keywords */
static boolean adaKeywordCmp(adaKeyword keyword)
{
  boolean status = FALSE;

  /* first check to see if line is empty, if it is, throw an exception */
  if(line == NULL)
  {
    exception = EXCEPTION_EOF;
    return status;
  }

  status = cmp(&line[pos], lineLen - pos, AdaKeywords[keyword]);

  /* if we match, increment the position pointer */
  if(status == TRUE)
  {
    matchLineNum = getSourceLineNumber();
    matchFilePos = getInputFilePosition();

    movePos((strlen(AdaKeywords[keyword])));
  }

  return status;
} /* static boolean adaKeywordCmp(adaKeyword keyword) */

static void skipUntilWhiteSpace(void)
{
  /* first check for a comment line, because this would cause the isspace
   * check to be true immediately */
  while(exception != EXCEPTION_EOF && isAdaComment(line, pos, lineLen))
  {
    readNewLine();
  }

  while(exception != EXCEPTION_EOF && !isspace(line[pos]))
  {
    /* don't use movePos() because if we read in a new line with this function
     * we need to stop */
    pos++;

    /* the newline counts as whitespace so read in the newline and return
     * immediately */
    if(pos >= lineLen)
    {
      line = (const char *) fileReadLine();
      pos = 0;

      if(line == NULL)
      {
        lineLen = 0;
        exception = EXCEPTION_EOF;
        return;
      }

      lineLen = strlen(line);

      return;
    } /* if(pos >= lineLen) */

    /* now check for comments here */
    while(exception != EXCEPTION_EOF && isAdaComment(line, pos, lineLen))
    {
      readNewLine();
    }
  } /* while(!isspace(line[pos])) */
} /* static void skipUntilWhiteSpace(void) */

static void skipWhiteSpace(void)
{
  /* first check for a comment line, because this would cause the isspace
   * check to fail immediately */
  while(exception != EXCEPTION_EOF && isAdaComment(line, pos, lineLen))
  {
    readNewLine();
  }

  while(exception != EXCEPTION_EOF && isspace(line[pos]))
  {
    movePos(1);

    /* now check for comments here */
    while(exception != EXCEPTION_EOF && isAdaComment(line, pos, lineLen))
    {
      readNewLine();
    }
  } /* while(isspace(line[pos])) */
} /* static void skipWhiteSpace(void) */

static void skipPast(const char *past)
{
  /* first check for a comment line, because this would cause the isspace
   * check to fail immediately */
  while(exception != EXCEPTION_EOF && isAdaComment(line, pos, lineLen))
  {
    readNewLine();
  }

  /* now look for the keyword */
  while(exception != EXCEPTION_EOF && !adaCmp(past))
  {
    movePos(1);

    /* now check for comments here */
    while(exception != EXCEPTION_EOF && isAdaComment(line, pos, lineLen))
    {
      readNewLine();
    }
  }
} /* static void skipPast(char *past) */

static void skipPastKeyword(adaKeyword keyword)
{
  /* first check for a comment line, because this would cause the isspace
   * check to fail immediately */
  while(exception != EXCEPTION_EOF && isAdaComment(line, pos, lineLen))
  {
    readNewLine();
  }

  /* now look for the keyword */
  while(exception != EXCEPTION_EOF && !adaKeywordCmp(keyword))
  {
    movePos(1);

    /* now check for comments here */
    while(exception != EXCEPTION_EOF && isAdaComment(line, pos, lineLen))
    {
      readNewLine();
    }
  }
} /* static void skipPastKeyword(adaKeyword keyword) */

static void skipPastWord(void)
{
  /* first check for a comment line, because this would cause the isspace
   * check to fail immediately */
  while(exception != EXCEPTION_EOF && isAdaComment(line, pos, lineLen))
  {
    readNewLine();
  }

  /* now increment until we hit a non-word character... Specifically,
   * whitespace, '(', ')', ':', and ';' */
  while(exception != EXCEPTION_EOF && !isspace(line[pos]) &&
        line[pos] != '(' && line[pos] != ')' && line[pos] != ':' &&
        line[pos] != ';')
  {
    /* don't use movePos() because if we read in a new line with this function
     * we need to stop */
    pos++;

    /* the newline counts as whitespace so read in the newline and return
     * immediately */
    if(pos >= lineLen)
    {
      line = (const char *) fileReadLine();
      pos = 0;

      if(line == NULL)
      {
        lineLen = 0;
        exception = EXCEPTION_EOF;
        return;
      }

      lineLen = strlen(line);

      return;
    } /* if(pos >= lineLen) */

    /* now check for comments here */
    while(exception != EXCEPTION_EOF && isAdaComment(line, pos, lineLen))
    {
      readNewLine();
    }
  } /* while(!isspace(line[pos])) */
} /* static void skipPastWord(void) */

static adaTokenInfo *adaParseBlock(adaTokenInfo *parent, adaKind kind)
{
  int i;
  adaTokenInfo *token;
  boolean isSpec = TRUE;

  skipWhiteSpace();

  /* if the next word is body, this is not a package spec */
  if(adaKeywordCmp(ADA_KEYWORD_BODY))
  {
    isSpec = FALSE;
  }
  /* if the next word is "type" then this has to be a task or protected spec */
  else if(adaKeywordCmp(ADA_KEYWORD_TYPE) &&
          (kind != ADA_KIND_PROTECTED && kind != ADA_KIND_TASK))
  {
    /* if this failed to validate then we should just fail */
    return NULL;
  }
  skipWhiteSpace();

  /* we are at the start of what should be the tag now... But we have to get
   * it's length.  So loop until we hit whitespace, init the counter to 1
   * since we know that the current position is not whitespace */
  for(i = 1; (pos + i) < lineLen && !isspace(line[pos + i]) &&
      line[pos + i] != '(' && line[pos + i] != ';'; i++);

  /* we have reached the tag of the package, so create the tag */
  token = newAdaToken(&line[pos], i, kind, isSpec, parent);

  movePos(i);
  skipWhiteSpace();

  /* task and protected types are allowed to have discriminants */
  if(exception != EXCEPTION_EOF && line[pos] == '(')
  {
    while(exception != EXCEPTION_EOF && line[pos] != ')')
    {
      movePos(1);
      adaParseVariables(token, ADA_KIND_AUTOMATIC_VARIABLE);
    }
    movePos(1);
  }

  /* we must parse until we hit the "is" string to reach the end of
   * this package declaration, or a "reanames" keyword */
  while(token != NULL)
  {
    skipWhiteSpace();

    if(adaKeywordCmp(ADA_KEYWORD_IS))
    {
      skipWhiteSpace();

      if(adaKeywordCmp(ADA_KEYWORD_SEPARATE))
      {
        /* if the next word is the keyword "separate", don't create the tag
         * since it will be defined elsewhere */
        freeAdaToken(&parent->children, token);
        token = NULL;

        /* move past the ";" ending this declaration */
        skipPast(";");
      }
      else if(adaKeywordCmp(ADA_KEYWORD_NEW))
      {
        /* if this is a "new" something then no need to parse */
        skipPast(";");
      }
      else
      {
        adaParse(ADA_DECLARATIONS, token);
      }

      break;
    } /* if(adaKeywordCmp(ADA_KEYWORD_IS)) */
    else if(adaKeywordCmp(ADA_KEYWORD_RENAMES))
    {
      skipPast(";");
      break;
    }
    else if(adaCmp(";"))
    {
      token->isSpec = TRUE;
      break;
    }
    else
    {
      /* nothing found, move to the next word */
      skipUntilWhiteSpace();
    }

    if (exception == EXCEPTION_EOF)
    {
      freeAdaToken(&parent->children, token);
      token = NULL;
    }
  } /* while(token != NULL) - while the end of spec, or beginning of body not found */

  return token;
} /* static adaTokenInfo *adaParseBlock(adaTokenInfo *parent, adaKind kind) */

static adaTokenInfo *adaParseSubprogram(adaTokenInfo *parent, adaKind kind)
{
  int i;
  adaTokenInfo *token;
  adaTokenInfo *tmpToken = NULL;

  skipWhiteSpace();

  /* we are at the start of what should be the tag now... But we have to get
   * it's length.  So loop until we hit whitespace or the beginning of the
   * parameter list.  Init the counter to 1 * since we know that the current
   * position is not whitespace */
  for(i = 1; (pos + i) < lineLen && !isspace(line[pos + i]) &&
      line[pos + i] != '(' && line[pos + i] != ';'; i++);

  /* we have reached the tag of the subprogram, so create the tag... Init the
   * isSpec flag to false and we will adjust it when we see if there is an
   * "is", "do" or a ";" following the tag */
  token = newAdaToken(&line[pos], i, kind, FALSE, parent);

  /* move the line position */
  movePos(i);
  skipWhiteSpace();

  /* if we find a '(' grab any parameters */
  if(exception != EXCEPTION_EOF && line[pos] == '(' && token != NULL)
  {
    while(exception != EXCEPTION_EOF && line[pos] != ')')
    {
      movePos(1);
      tmpToken = adaParseVariables(token, ADA_KIND_AUTOMATIC_VARIABLE);
    }
    movePos(1);

    /* check to see if anything was received... If this is an entry this may
     * have a 'discriminant' and not have any parameters in the first
     * parenthesis pair, so check again if this was the case*/
    if(kind == ADA_KIND_ENTRY && tmpToken == NULL)
    {
      /* skip any existing whitespace and see if there is a second parenthesis
       * pair */
      skipWhiteSpace();

      if(exception != EXCEPTION_EOF && line[pos] == '(')
      {
        while(exception != EXCEPTION_EOF && line[pos] != ')')
        {
          movePos(1);
          adaParseVariables(token, ADA_KIND_AUTOMATIC_VARIABLE);
        }
        movePos(1);
      }
    } /* if(kind == ADA_KIND_ENTRY && tmpToken == NULL) */
  } /* if(line[pos] == '(' && token != NULL) */

  /* loop infinitely until we hit a "is", "do" or ";", this will skip over
   * the returns keyword, returned-type for functions as well as any one of a
   * myriad of keyword qualifiers */
  while(exception != EXCEPTION_EOF && token != NULL)
  {
    skipWhiteSpace();

    if(adaKeywordCmp(ADA_KEYWORD_IS))
    {
      skipWhiteSpace();

      if(adaKeywordCmp(ADA_KEYWORD_SEPARATE))
      {
        /* if the next word is the keyword "separate", don't create the tag
         * since it will be defined elsewhere */
        freeAdaToken(&parent->children, token);
        token = NULL;

        /* move past the ";" ending this declaration */
        skipPast(";");
      }
      else if(adaKeywordCmp(ADA_KEYWORD_NEW))
      {
        /* if this is a "new" something then no need to parse */
        skipPast(";");
      }
      else
      {
        adaParse(ADA_DECLARATIONS, token);
      }

      break;
    } /* if(adaKeywordCmp(ADA_KEYWORD_IS)) */
    else if(adaKeywordCmp(ADA_KEYWORD_RENAMES))
    {
      skipPast(";");
      break;
    }
    else if(adaKeywordCmp(ADA_KEYWORD_DO))
    {
      /* do is the keyword for the beginning of a task entry */
      adaParse(ADA_CODE, token);
      break;
    }
    else if(adaCmp(";"))
    {
      /* this is just a spec then, so set the flag in the token */
      token->isSpec = TRUE;
      break;
    }
    else
    {
      /* nothing found, move to the next word */
      movePos(1); /* make sure to advance even if we aren't actually on a word */
      skipPastWord();
    }
  } /* while(exception != EXCEPTION_EOF && token != NULL) */

  return token;
} /* static adaTokenInfo *adaParseSubprogram(adaTokenInfo *parent, ... ) */

static adaTokenInfo *adaParseType(adaTokenInfo *parent, adaKind kind)
{
  int i;
  adaTokenInfo *token = NULL;

  skipWhiteSpace();

  /* get the name of the type */
  for(i = 1; (pos + i) < lineLen && !isspace(line[pos + i]) &&
      line[pos + i] != '(' && line[pos + i] != ';'; i++);

  token = newAdaToken(&line[pos], i, kind, FALSE, parent);

  movePos(i);
  skipWhiteSpace();

  if(exception != EXCEPTION_EOF && line[pos] == '(')
  {
    /* in this case there is a discriminant to this type, gather the
     * variables */
    while(exception != EXCEPTION_EOF && line[pos] != ')')
    {
      movePos(1);
      adaParseVariables(token, ADA_KIND_AUTOMATIC_VARIABLE);
    }
    movePos(1);
    skipWhiteSpace();
  }

  /* check to see what is next, if it is not "is" then just skip to the end of
   * the statement and register this as a 'spec' */
  if(adaKeywordCmp(ADA_KEYWORD_IS))
  {
    skipWhiteSpace();
    /* check to see if this may be a record or an enumeration */
    if(exception != EXCEPTION_EOF && line[pos] == '(')
    {
      movePos(1);
      adaParseVariables(token, ADA_KIND_ENUM_LITERAL);
    }
    else if(adaKeywordCmp(ADA_KEYWORD_RECORD))
    {
      /* until we hit "end record" we need to gather type variables */
      while(exception != EXCEPTION_EOF)
      {
        skipWhiteSpace();

        if(adaKeywordCmp(ADA_KEYWORD_END))
        {
          skipWhiteSpace();
          if(adaKeywordCmp(ADA_KEYWORD_RECORD))
          {
            break;
          }
          skipPast(";");
        } /* if(adaKeywordCmp(ADA_KEYWORD_END)) */
        /* handle variant types */
        else if(adaKeywordCmp(ADA_KEYWORD_CASE))
        {
          skipPastKeyword(ADA_KEYWORD_IS);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_WHEN))
        {
          skipPast("=>");
        }
        else
        {
          adaParseVariables(token, ADA_KIND_RECORD_COMPONENT);
          skipPast(";");
        }
      } /* while(TRUE) - end of record not found */
    } /* else if(adaKeywordCmp(ADA_KEYWORD_RECORD)) */
  } /* if(adaKeywordCmp(ADA_KEYWORD_IS)) */
  else
  {
    token->isSpec = TRUE;
  }

  skipPast(";");

  return token;
} /* static adaTokenInfo *adaParseType(adaTokenInfo *parent, adaKind kind) */

static adaTokenInfo *adaParseVariables(adaTokenInfo *parent, adaKind kind)
{
  /* variables for keeping track of tags */
  int varEndPos = -1;
  int tokenStart = -1;
  adaTokenInfo *token = NULL;

  /* buffer management variables */
  int i = 0;
  int bufPos = 0;
  int bufLen = 0;
  char *buf = NULL;

  /* file and line position variables */
  unsigned long int lineNum;
  int filePosIndex = 0;
  int filePosSize = 32;
  fpos_t *filePos = xMalloc(filePosSize, fpos_t);

  /* skip any preliminary whitespace or comments */
  skipWhiteSpace();
  while(exception != EXCEPTION_EOF && isAdaComment(line, pos, lineLen))
  {
    readNewLine();
  }

  /* before we start reading input save the current line number and file
   * position, so we can reconstruct the correct line & file position for any
   * tags we create */
  lineNum = getSourceLineNumber();
  filePos[filePosIndex] = getInputFilePosition();

  /* setup local buffer... Since we may have to read a few lines to verify
   * that this is a proper variable declaration, and still make a token for
   * each variable, add one to the allocated string to account for a '\0' */
  bufLen = lineLen - pos;
  buf = xMalloc(bufLen + 1, char);
  memcpy((void *) buf, (void *) &line[pos], bufLen);

  /* don't increase bufLen to include the NULL char so that strlen(buf) and
   * bufLen match */
  buf[bufLen] = '\0';

  while(exception != EXCEPTION_EOF)
  {
    /* make sure that we don't count anything in a comment as being valid to
     * parse */
    if(isAdaComment(buf, bufPos, bufLen))
    {
      /* move bufPos to the end of this 'line' so a new line of input is
       * read */
      bufPos = bufLen - 1;

      /* if tokenStart is not -2 then we may be trying to track the type
       * of this variable declaration, so set tokenStart to -1 so that the
       * tracking can start over */
      if(tokenStart != -2)
      {
        tokenStart = -1;
      }
    } /* if(isAdaComment(buf, bufPos, bufLen)) */
    /* we have to keep track of any () pairs that may be in the variable
     * declarations.  And then quit if we hit a ';' the real end ')', or also
     * a variable initialization... Once we hit := then we have hit the end of
     * the variable declaration */
    else if(buf[bufPos] == '(')
    {
      i++;
    }
    else if(buf[bufPos] == ')')
    {
      if(i == 0)
      {
        break;
      }
      else
      {
        i--;
      }
    }
    else if(buf[bufPos] == ';' ||
            ((bufPos + 1) < bufLen &&
             (strncasecmp(&buf[bufPos], ":=", strlen(":=")) == 0 ||
              strncasecmp(&buf[bufPos], "=>", strlen("=>")) == 0)))
    {
      break;
    }
    /* if we found the : keep track of where we found it */
    else if(buf[bufPos] == ':' &&
            (bufPos + 1 >= bufLen || buf[bufPos + 1] != '='))
    {
      varEndPos = bufPos;
    }
    /* if we have the position of the ':' find out what the next word is,
     * because if it "constant" or "exception" then we must tag this slightly
     * differently, But only check this for normal variables */
    else if(kind == ADA_KIND_VARIABLE && varEndPos != -1 &&
            !isspace(buf[bufPos]) && tokenStart == -1)
    {
      tokenStart = bufPos;
    }
    else if(kind == ADA_KIND_VARIABLE && varEndPos != -1 && tokenStart >= 0 &&
            ((bufPos + 1) >= bufLen || isspace(buf[bufPos + 1]) ||
             buf[bufPos + 1] == ';'))
    {
      if(cmp(&buf[tokenStart], bufLen - tokenStart,
             AdaKeywords[ADA_KEYWORD_CONSTANT]) == TRUE)
      {
        kind = ADA_KIND_CONSTANT;
      }
      else if(cmp(&buf[tokenStart], bufLen - tokenStart,
                  AdaKeywords[ADA_KEYWORD_EXCEPTION]) == TRUE)
      {
        kind = ADA_KIND_EXCEPTION;
      }

      /* set tokenStart to -2 to prevent any more words from being checked */
      tokenStart = -2;
    }

    bufPos++;

    /* if we just incremented beyond the length of the current buffer, we need
     * to read in a new line */
    if(exception != EXCEPTION_EOF && bufPos >= bufLen)
    {
      readNewLine();

      /* store the new file position for the start of this line */
      filePosIndex++;
      while(filePosIndex >= filePosSize)
      {
        filePosSize *= 2;
        filePos = xRealloc(filePos, filePosSize, fpos_t);
      }
      filePos[filePosIndex] = getInputFilePosition();

      /* increment bufLen and bufPos now so that they jump past the NULL
       * character in the buffer */
      bufLen++;
      bufPos++;

      /* allocate space and store this into our buffer */
      bufLen += lineLen;
      buf = xRealloc(buf, bufLen + 1, char);
      memcpy((void *) &buf[bufPos], (void *) line, lineLen);
      buf[bufLen] = '\0';
    } /* if(bufPos >= bufLen) */
  } /* while(exception != EXCEPTION_EOF) */

  /* There is a special case if we are gathering enumeration values and we hit
   * a ')', that is allowed so we need to move varEndPos to where the ')' is */
  if(kind == ADA_KIND_ENUM_LITERAL && buf[bufPos] == ')' && varEndPos == -1)
  {
    varEndPos = bufPos;
  }

  /* so we found a : or ;... If it is a : go back through the buffer and
   * create a token for each word skipping over all whitespace and commas
   * until the : is hit*/
  if(varEndPos != -1)
  {
    /* there should be no whitespace at the beginning, so tokenStart is
     * initialized to 0 */
    tokenStart = 0;

    /* before we start set the filePosIndex back to 0 so we can go through the
     * file position table as the read line number increases */
    filePosIndex = 0;

    for(i = 0; i < varEndPos; i++)
    {
      /* skip comments which are '--' unless we are in a word */
      if(isAdaComment(buf, i, varEndPos))
      {
        /* move i past the '\0' that we put at the end of each line stored in
         * buf */
        for( ; i < varEndPos && buf[i] != '\0'; i++);
      } /* if(isAdaComment(buf, i, varEndPos)) */
      else if(tokenStart != -1 && (isspace(buf[i]) || buf[i] == ',' ||
              buf[i] == '\0'))
      {
        /* only store the word if it is not an in/out keyword */
        if(!cmp(&buf[tokenStart], varEndPos, "in") &&
           !cmp(&buf[tokenStart], varEndPos, "out"))
        {
          token = newAdaToken(&buf[tokenStart], i - tokenStart,
                              kind, FALSE, parent);

          /* now set the proper line and file position counts for this
           * new token */
          token->tag.lineNumber = lineNum + filePosIndex;
          token->tag.filePosition = filePos[filePosIndex];
        }
        tokenStart = -1;
      } /* if(tokenStart != -1 && (isspace(buf[i]) || buf[i] == ',')) */
      else if(tokenStart == -1 && !(isspace(buf[i]) || buf[i] == ',' ||
              buf[i] == '\0'))
      {
        /* only set the tokenStart for non-newline characters */
        tokenStart = i;
      }

      /* after we are finished with this line, move the file position */
      if(buf[i] == '\0')
      {
        filePosIndex++;
      }
    } /* for(i = 0; i < varEndPos; i++) */

    /* if token start was 'started' then we should store the last token */
    if(tokenStart != -1)
    {
      token = newAdaToken(&buf[tokenStart], i - tokenStart,
                          kind, FALSE, parent);

      /* now set the proper line and file position counts for this
       * new token */
      token->tag.lineNumber = lineNum + filePosIndex;
      token->tag.filePosition = filePos[filePosIndex];
    }
  } /* if(varEndPos != -1) */

  /* now get the pos variable to point to the correct place in line where we
   * left off in our temp buf, and free our temporary buffer.  This is a
   * little different than most buf position moves.  It gets the distance from
   * the current buf position to the end of the buffer, which is also the
   * distance from where pos should be wrt the end of the variable
   * definition */
  movePos((lineLen - (bufLen - bufPos)) - pos);
  eFree((void *) buf);
  eFree((void *) filePos);

  return token;
} /* static adaTokenInfo *adaParseVariables(adaTokenInfo *parent, ... ) */

static adaTokenInfo *adaParseLoopVar(adaTokenInfo *parent)
{
  int i;
  adaTokenInfo *token = NULL;

  skipWhiteSpace();
  for(i = 1; (pos + i) < lineLen && !isspace(line[pos + i]); i++);
  token = newAdaToken(&line[pos], i, ADA_KIND_AUTOMATIC_VARIABLE, FALSE,
                      parent);
  movePos(i);

  /* now skip to the end of the loop declaration */
  skipPastKeyword(ADA_KEYWORD_LOOP);

  return token;
} /* static adaTokenInfo *adaParseLoopVar(adaTokenInfo *parent) */

static adaTokenInfo *adaParse(adaParseMode mode, adaTokenInfo *parent)
{
  int i;
  adaTokenInfo genericParamsRoot;
  adaTokenInfo *token = NULL;

  initAdaTokenList(&genericParamsRoot.children);

  /* if we hit the end of the file, line will be NULL and our skip and match
   * functions will hit this jump buffer with EXCEPTION_EOF */
  while(exception == EXCEPTION_NONE)
  {
    /* find the next place to start */
    skipWhiteSpace();

    /* check some universal things to check for first */
    if(exception == EXCEPTION_EOF)
    {
      break;
    }
    else if(isAdaComment(line, pos, lineLen))
    {
      readNewLine();
      continue;
    }
    else if(adaKeywordCmp(ADA_KEYWORD_PRAGMA) ||
            adaKeywordCmp(ADA_KEYWORD_WITH) ||
            adaKeywordCmp(ADA_KEYWORD_USE))
    {
      /* set the token to NULL so we accidentally don't pick up something
       * from earlier */
      skipPast(";");
      continue;
    }

    /* check for tags based on our current mode */
    switch(mode)
    {
      case ADA_ROOT:
        if(adaKeywordCmp(ADA_KEYWORD_PACKAGE))
        {
          token = adaParseBlock(parent, ADA_KIND_PACKAGE);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_PROCEDURE) ||
                adaKeywordCmp(ADA_KEYWORD_FUNCTION))
        {
          token = adaParseSubprogram(parent, ADA_KIND_SUBPROGRAM);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_TASK))
        {
          token = adaParseBlock(parent, ADA_KIND_TASK);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_PROTECTED))
        {
          token = adaParseBlock(parent, ADA_KIND_PROTECTED);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_GENERIC))
        {
          /* if we have hit a generic declaration, go to the generic section
           * and collect the formal parameters */
          mode = ADA_GENERIC;
          break;
        } /* else if(adaKeywordCmp(ADA_KEYWORD_GENERIC)) */
        else if(adaKeywordCmp(ADA_KEYWORD_SEPARATE))
        {
          /* skip any possible whitespace */
          skipWhiteSpace();

          /* skip over the "(" until we hit the tag */
          if(exception != EXCEPTION_EOF && line[pos] == '(')
          {
            movePos(1);
            skipWhiteSpace();

            /* get length of tag */
            for(i = 1; (pos + i) < lineLen && line[pos + i] != ')' &&
                !isspace(line[pos + i]); i++);

            /* if this is a separate declaration, all it really does is create
             * a false high level token for everything in this file to belong
             * to... But we don't know what kind it is, so we declare it as
             * ADA_KIND_SEPARATE, which will cause it not to be placed in
             * the tag file, and the item in this file will be printed as
             * separate:<name> instead of package:<name> or whatever the
             * parent kind really is (assuming the ctags option will be on
             * for printing such info to the tag file) */
            token = newAdaToken(&line[pos], i, ADA_KIND_SEPARATE, FALSE,
                                parent);

            /* since this is a false top-level token, set parent to be
             * token */
            parent = token;
            token = NULL;

            /* skip past the ')' */
            skipPast(")");
          } /* if(line[pos] == '(') */
          else
          {
            /* move to the end of this statement */
            skipPast(";");
          }
        } /* else if(adaKeywordCmp(ADA_KEYWORD_SEPARATE)) */
        else
        {
          /* otherwise, nothing was found so just skip until the end of this
           * unknown statement... It's most likely just a use or with
           * clause.  Also set token to NULL so we don't attempt anything
           * incorrect */
          token = NULL;
          skipPast(";");
        }

        /* check to see if we succeeded in creating our token */
        if(token != NULL)
        {
          /* if any generic params have been gathered, attach them to
           * token */
          appendAdaTokenList(token, &genericParamsRoot.children);
        } /* if(token != NULL) */

        break;

      case ADA_GENERIC:
        /* if we are processing a generic block, make up some temp children
         * which we will later attach to the root of the real
         * procedure/package/whatever the formal parameters are for */
        if(adaKeywordCmp(ADA_KEYWORD_PACKAGE))
        {
          token = adaParseBlock(parent, ADA_KIND_PACKAGE);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_PROCEDURE) ||
                adaKeywordCmp(ADA_KEYWORD_FUNCTION))
        {
          token = adaParseSubprogram(parent, ADA_KIND_SUBPROGRAM);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_TASK))
        {
          token = adaParseBlock(parent, ADA_KIND_TASK);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_PROTECTED))
        {
          token = adaParseBlock(parent, ADA_KIND_PROTECTED);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_TYPE))
        {
          skipWhiteSpace();

          /* get length of tag */
          for(i = 1; (pos + i) < lineLen && !isspace(line[pos + i]) &&
              line[pos + i] != '(' && line[pos + i] != ';'; i++);

          appendAdaToken(&genericParamsRoot,
                         newAdaToken(&line[pos], i, ADA_KIND_FORMAL, FALSE,
                                     NULL));

          /* skip to the end of this formal type declaration */
          skipPast(";");
        } /* else if(adaKeywordCmp(ADA_KEYWORD_TYPE)) */
        else if(adaKeywordCmp(ADA_KEYWORD_WITH))
        {
          skipWhiteSpace();
          /* skip over the function/procedure keyword, it doesn't matter for
           * now */
          skipUntilWhiteSpace();
          skipWhiteSpace();

          /* get length of tag */
          for(i = 1; (pos + i) < lineLen && !isspace(line[pos + i]) &&
              line[pos + i] != '(' && line[pos + i] != ';'; i++);

          appendAdaToken(&genericParamsRoot,
                         newAdaToken(&line[pos], i, ADA_KIND_FORMAL, FALSE,
                                     NULL));

          /* increment the position */
          movePos(i);

          /* now gather the parameters to this subprogram */
          if(exception != EXCEPTION_EOF && line[pos] == '(')
          {
            while(exception != EXCEPTION_EOF && line[pos] != ')')
            {
              movePos(1);
              adaParseVariables(genericParamsRoot.children.tail,
                                ADA_KIND_AUTOMATIC_VARIABLE);
            }
            movePos(1);
          }

          /* skip to the end of this formal type declaration */
          skipPast(";");
        } /* else if(adaKeywordCmp(ADA_KEYWORD_WITH)) */
        else
        {
          /* otherwise, nothing was found so just skip until the end of this
           * unknown statement... It's most likely just a use or with
           * clause.  Also set token to NULL so we don't attempt anything
           * incorrect */
          token = NULL;
          skipPast(";");
        }

        /* check to see if we succeeded in creating our token */
        if(token != NULL)
        {
          /* if any generic params have been gathered, attach them to
           * token, and set the mode back to ADA_ROOT */
          appendAdaTokenList(token, &genericParamsRoot.children);
          mode = ADA_ROOT;
        } /* if(token != NULL) */

        break;

      case ADA_DECLARATIONS:
        if(adaKeywordCmp(ADA_KEYWORD_PACKAGE))
        {
          token = adaParseBlock(parent, ADA_KIND_PACKAGE);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_PROCEDURE) ||
                adaKeywordCmp(ADA_KEYWORD_FUNCTION))
        {
          token = adaParseSubprogram(parent, ADA_KIND_SUBPROGRAM);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_TASK))
        {
          token = adaParseBlock(parent, ADA_KIND_TASK);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_PROTECTED))
        {
          token = adaParseBlock(parent, ADA_KIND_PROTECTED);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_GENERIC))
        {
          /* if we have hit a generic declaration, go to the generic section
           * and collect the formal parameters */
          mode = ADA_GENERIC;
          break;
        }
        else if(adaKeywordCmp(ADA_KEYWORD_TYPE))
        {
          token = adaParseType(parent, ADA_KIND_TYPE);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_SUBTYPE))
        {
          token = adaParseType(parent, ADA_KIND_SUBTYPE);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_BEGIN))
        {
          mode = ADA_CODE;
          break;
        }
        else if(adaKeywordCmp(ADA_KEYWORD_FOR))
        {
          /* if we hit a "for" statement it is defining implementation details
           * for a specific type/variable/subprogram/etc...  So we should just
           * skip it, so skip the tag, then we need to see if there is a
           * 'record' keyword... If there is we must skip past the
           * 'end record;' statement.  First skip past the tag */
          skipPastKeyword(ADA_KEYWORD_USE);
          skipWhiteSpace();

          if(adaKeywordCmp(ADA_KEYWORD_RECORD))
          {
            /* now skip to the next "record" keyword, which should be the end
             * of this use statement */
            skipPastKeyword(ADA_KEYWORD_RECORD);
          }

          /* lastly, skip past the end ";" */
          skipPast(";");
        }
        else if(adaKeywordCmp(ADA_KEYWORD_END))
        {
          /* if we have hit an end then we must see if the next word matches
           * the parent token's name.  If it does we hit the end of whatever
           * sort of block construct we were processing and we must
           * return */
          skipWhiteSpace();
          if(adaCmp(parent->name))
          {
            skipPast(";");

            /* return the token */
            freeAdaTokenList(&genericParamsRoot.children);
            return token;
          } /* if(adaCmp(parent->name)) */
          else
          {
            /* set the token to NULL so we accidentally don't pick up something
             * from earlier */
            token = NULL;
            skipPast(";");
          }
        } /* else if(adaKeywordCmp(ADA_KEYWORD_END)) */
        else if(adaKeywordCmp(ADA_KEYWORD_ENTRY))
        {
          token = adaParseSubprogram(parent, ADA_KIND_ENTRY);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_PRIVATE))
        {
          /* if this is a private declaration then we need to set the global 
           * file spec flag and then skip whitespace to get to the next bit of 
           * code to parse. */
          if(parent != NULL)
          {
            parent->isPrivate = TRUE;
          }

          skipWhiteSpace();
        }
        else
        {
          /* if nothing else matched this is probably a variable, constant
           * or exception declaration */
          token = adaParseVariables(parent, ADA_KIND_VARIABLE);
          skipPast(";");
        }

        /* check to see if we succeeded in creating our token */
        if(token != NULL)
        {
          /* if this is one of the root-type tokens... Do some extra
           * processing */
          if(token->kind == ADA_KIND_PACKAGE ||
             token->kind == ADA_KIND_SUBPROGRAM ||
             token->kind == ADA_KIND_TASK ||
             token->kind == ADA_KIND_PROTECTED)
          {
            /* if any generic params have been gathered, attach them to
             * token */
            appendAdaTokenList(token, &genericParamsRoot.children);
          }
        } /* if(token != NULL) */
        break;

      case ADA_CODE:
        if(adaKeywordCmp(ADA_KEYWORD_DECLARE))
        {
          /* if we are starting a declare block here, and not down at the
           * identifier definition then make an anonymous token to track the
           * data in this block */
          token = newAdaToken(NULL, 0, ADA_KIND_ANONYMOUS, FALSE, parent);

          /* save the correct starting line */
          token->tag.lineNumber = matchLineNum;
          token->tag.filePosition = matchFilePos;

          adaParse(ADA_DECLARATIONS, token);
        } /* if(adaKeywordCmp(ADA_KEYWORD_DECLARE)) */
        else if(adaKeywordCmp(ADA_KEYWORD_BEGIN))
        {
          /* if we are starting a code block here, and not down at the
           * identifier definition then make an anonymous token to track the
           * data in this block, if this was part of a proper LABEL:
           * declare/begin/end block then the parent would already be a label
           * and this begin statement would have been found while in the
           * ADA_DECLARATIONS parsing section  */
          token = newAdaToken(NULL, 0, ADA_KIND_ANONYMOUS, FALSE, parent);

          /* save the correct starting line */
          token->tag.lineNumber = matchLineNum;
          token->tag.filePosition = matchFilePos;

          adaParse(ADA_CODE, token);
        } /* else if(adaKeywordCmp(ADA_KEYWORD_BEGIN)) */
        else if(adaKeywordCmp(ADA_KEYWORD_EXCEPTION))
        {
          mode = ADA_EXCEPTIONS;
          break;
        } /* else if(adaKeywordCmp(ADA_KEYWORD_EXCEPTION)) */
        else if(adaKeywordCmp(ADA_KEYWORD_END))
        {
          /* if we have hit an end then we must see if the next word matches
           * the parent token's name.  If it does we hit the end of whatever
           * sort of block construct we were processing and we must
           * return */
          skipWhiteSpace();
          if(adaCmp(parent->name))
          {
            skipPast(";");

            /* return the token */
            freeAdaTokenList(&genericParamsRoot.children);
            return token;
          } /* if(adaCmp(parent->name)) */
          else if(adaKeywordCmp(ADA_KEYWORD_LOOP))
          {
            /* a loop with an identifier has this syntax:
             * "end loop <ident>;" */
            skipWhiteSpace();

            /* now check for the parent loop's name */
            if(adaCmp(parent->name))
            {
              skipPast(";");

              /* return the token */
              freeAdaTokenList(&genericParamsRoot.children);
              return token;
            } /* if(adaCmp(parent->name)) */
          } /* else if(adaKeywordCmp(ADA_KEYWORD_LOOP)) */
          else
          {
            /* otherwise, nothing was found so just skip until the end of
             * this statement */
            skipPast(";");
          }
        } /* else if(adaKeywordCmp(ADA_KEYWORD_END)) */
        else if(adaKeywordCmp(ADA_KEYWORD_ACCEPT))
        {
          adaParseSubprogram(parent, ADA_KIND_ENTRY);
        } /* else if(adaKeywordCmp(ADA_KEYWORD_ACCEPT)) */
        else if(adaKeywordCmp(ADA_KEYWORD_FOR))
        {
          /* if this is a for loop, then we may need to pick up the
           * automatic loop iterator, But... The loop variable is only
           * available within the loop itself so make an anonymous label
           * parent for this loop var to be parsed in */
          token = newAdaToken(AdaKeywords[ADA_KEYWORD_LOOP],
                              strlen(AdaKeywords[ADA_KEYWORD_LOOP]),
                              ADA_KIND_ANONYMOUS, FALSE, parent);
          adaParseLoopVar(token);
          adaParse(ADA_CODE, token);
        } /* else if(adaKeywordCmp(ADA_KEYWORD_FOR)) */
        else if(adaKeywordCmp(ADA_KEYWORD_WHILE))
        {
          token = newAdaToken(AdaKeywords[ADA_KEYWORD_LOOP],
                              strlen(AdaKeywords[ADA_KEYWORD_LOOP]),
                              ADA_KIND_ANONYMOUS, FALSE, parent);

          /* skip past the while loop declaration and parse the loop body */
          skipPastKeyword(ADA_KEYWORD_LOOP);
          skipWhiteSpace();
          adaParse(ADA_CODE, token);
        } /* else if(adaKeywordCmp(ADA_KEYWORD_WHILE)) */
        else if(adaKeywordCmp(ADA_KEYWORD_LOOP))
        {
          token = newAdaToken(AdaKeywords[ADA_KEYWORD_LOOP],
                              strlen(AdaKeywords[ADA_KEYWORD_LOOP]),
                              ADA_KIND_ANONYMOUS, FALSE, parent);

          /* save the correct starting line */
          token->tag.lineNumber = matchLineNum;
          token->tag.filePosition = matchFilePos;

          /* parse the loop body */
          skipWhiteSpace();
          adaParse(ADA_CODE, token);
        } /* else if(adaKeywordCmp(ADA_KEYWORD_LOOP)) */
        else if(line != NULL &&
                strncasecmp(&line[pos], "<<", strlen("<<")) == 0)
        {
          movePos(strlen("<<"));

          /* if the first chars are <<, find the ending >> and if we do that
           * then store the label tag, start i at strlen of "<<" plus 1
           * because we don't want to move the real pos until we know for
           * sure this is a label */
          for(i = 1; (pos + i) < lineLen &&
              strncasecmp(&line[pos + i], ">>", strlen(">>")) != 0;
              i++);

          /* if we didn't increment to the end of the line, a match was
           * found, if we didn't just fall through */
          if((pos + i) < lineLen)
          {
            token = newAdaToken(&line[pos], i, ADA_KIND_LABEL, FALSE, parent);
            skipPast(">>");
            token = NULL;
          }
        } /* else if(strncasecmp(line[pos], "<<", strlen("<<")) == 0) */
        /* we need to check for a few special case keywords that might cause
         * the simple ; ending statement checks to fail, first the simple
         * one word keywords and then the start <stuff> end statements */
        else if(adaKeywordCmp(ADA_KEYWORD_SELECT) ||
                adaKeywordCmp(ADA_KEYWORD_OR) ||
                adaKeywordCmp(ADA_KEYWORD_ELSE))
        {
          skipWhiteSpace();
        }
        else if(adaKeywordCmp(ADA_KEYWORD_IF) ||
                adaKeywordCmp(ADA_KEYWORD_ELSIF))
        {
          skipPastKeyword(ADA_KEYWORD_THEN);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_CASE))
        {
          skipPastKeyword(ADA_KEYWORD_IS);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_WHEN))
        {
          skipPast("=>");
        }
        else
        {
          /* set token to NULL so we don't accidentally not find an identifier,
           * But then fall through to the != NULL check */
          token = NULL;

          /* there is a possibility that this may be a loop or block
           * identifier, so check for a <random_word>: statement */
          for(i = 1; (pos + i) < lineLen; i++)
          {
            /* if we hit a non-identifier character (anything But letters, _
             * and ':' then this is not an identifier */
            if(!isalnum(line[pos + i]) && line[pos + i] != '_' &&
               line[pos + i] != ':')
            {
              /* if this is not an identifier then we should just bail out of
               * this loop now */
              break;
            }
            else if((line[pos + i] == ':') && (line[pos + i + 1] != '='))
            {
              token = newAdaToken(&line[pos], i, ADA_KIND_IDENTIFIER, FALSE,
                                  parent);
              break;
            }
          } /* for(i = 1; (pos + i) < lineLen; i++) */

          /* if we created a token, we found an identifier.  Now check for a
           * declare or begin statement to see if we need to start parsing
           * the following code like a root-style token would */
          if(token != NULL)
          {
            /* if something was found, reset the position variable and try to
             * find the next item */
            movePos(i + 1);
            skipWhiteSpace();

            if(adaKeywordCmp(ADA_KEYWORD_DECLARE))
            {
              adaParse(ADA_DECLARATIONS, token);
            }
            else if(adaKeywordCmp(ADA_KEYWORD_BEGIN))
            {
              adaParse(ADA_CODE, token);
            }
            else if(adaKeywordCmp(ADA_KEYWORD_FOR))
            {
              /* just grab the automatic loop variable, and then parse the
               * loop (it may have something to tag which will be a 'child'
               * of the loop) */
              adaParseLoopVar(token);
              adaParse(ADA_CODE, token);
            }
            else if(adaKeywordCmp(ADA_KEYWORD_WHILE))
            {
              /* skip to the loop keyword */
              skipPastKeyword(ADA_KEYWORD_LOOP);
              skipWhiteSpace();

              /* parse the loop (it may have something to tag which will be
               * a 'child' of the loop) */
              adaParse(ADA_CODE, token);
            } /* else if(adaKeywordCmp(ADA_KEYWORD_WHILE)) */
            else if(adaKeywordCmp(ADA_KEYWORD_LOOP))
            {
              skipWhiteSpace();

              /* parse the loop (it may have something to tag which will be
               * a 'child' of the loop) */
              adaParse(ADA_CODE, token);
            }
            else
            {
              /* otherwise, nothing was found so this is not a valid
               * identifier, delete it */
              freeAdaToken(&parent->children, token);
              token = NULL;
            }
          } /* if(token != NULL) */
          else
          {
            /* since nothing was found, simply skip to the end of this
             * statement */
            skipPast(";");
          }
        } /* else... No keyword tag fields found, look for others such as
           * loop and declare identifiers labels or just skip over this
           * line */

        break;

      case ADA_EXCEPTIONS:
        if(adaKeywordCmp(ADA_KEYWORD_PRAGMA))
        {
          skipPast(";");
        }
        else if(adaKeywordCmp(ADA_KEYWORD_WHEN))
        {
          skipWhiteSpace();
          token = adaParseVariables(parent, ADA_KIND_AUTOMATIC_VARIABLE);
        }
        else if(adaKeywordCmp(ADA_KEYWORD_END))
        {
          /* if we have hit an end then we must see if the next word matches
           * the parent token's name.  If it does we hit the end of whatever
           * sort of block construct we were processing and we must
           * return */
          skipWhiteSpace();
          if(adaCmp(parent->name))
          {
            skipPast(";");

            /* return the token */
            freeAdaTokenList(&genericParamsRoot.children);
            return token;
          } /* if(adaCmp(parent->name)) */
          else
          {
            /* otherwise, nothing was found so just skip until the end of
             * this statement */
            skipPast(";");
          }
        } /* else if(adaKeywordCmp(ADA_KEYWORD_END)) */
        else
        {
          /* otherwise, nothing was found so just skip until the end of
           * this statement */
          skipPast(";");
        }

        break;

      default:
        Assert(0);
    } /* switch(mode) */
  } /* while(exception == EXCEPTION_NONE)  */

  freeAdaTokenList(&genericParamsRoot.children);
  return token;
} /* static adaTokenInfo *adaParse(adaParseMode mode, adaTokenInfo *parent) */

static void storeAdaTags(adaTokenInfo *token, const char *parentScope)
{
  char *currentScope = NULL;
  adaTokenInfo *tmp = NULL;

  if(token != NULL)
  {
    /* do a spec transition if necessary */
    if(token->isSpec == TRUE)
    {
      makeSpec(&token->kind);

      if(token->kind != ADA_KIND_UNDEFINED)
      {
        token->tag.kind = &(AdaKinds[token->kind]);
      }
    }

    /* fill in the scope data */
    if(token->parent != NULL)
    {
      if(token->parent->kind > ADA_KIND_UNDEFINED &&
         token->parent->kind < ADA_KIND_COUNT)
      {
        token->tag.extensionFields.scope[0] =
          AdaKinds[token->parent->kind].name;
        token->tag.extensionFields.scope[1] = token->parent->name;
      }
      else if(token->parent->kind == ADA_KIND_SEPARATE)
      {
        token->tag.extensionFields.scope[0] =
          AdaKeywords[ADA_KEYWORD_SEPARATE];
        token->tag.extensionFields.scope[1] = token->parent->name;
      }
    } /* else if(token->parent->kind == ADA_KIND_ANONYMOUS) */
  } /* if(token->parent != NULL) */

  /* one check before we try to make a tag... If this is an anonymous
   * declare block then it's name is empty.  Give it one */
  if(token->kind == ADA_KIND_ANONYMOUS && token->name == NULL)
  {
    token->name = (char *) AdaKeywords[ADA_KEYWORD_DECLARE];
    token->tag.name = AdaKeywords[ADA_KEYWORD_DECLARE];
  }

  /* Now 'make' tags that have their options set, But only make anonymous
   * tags if they have children tags.  Also, don't make this tag if the file 
   * scope flag is not set and this tag is a file scope tag. */
  if((token->kind > ADA_KIND_UNDEFINED) && (token->kind < ADA_KIND_COUNT) &&
     (AdaKinds[token->kind].enabled == TRUE) &&
     (token->name != NULL) &&
     ((token->kind == ADA_KIND_ANONYMOUS && token->children.head != NULL) ||
      token->kind != ADA_KIND_ANONYMOUS) &&
     ((Option.include.fileScope == TRUE) ||
      ((Option.include.fileScope == FALSE) &&
       (token->tag.isFileScope == FALSE))))
  {
    makeTagEntry(&token->tag);

    /* before making the tag, if the --extra=+q flag is set we should create 
     * an extra entry which is the full parent.tag name.  But only do this if 
     * the parentScope flag is not NULL, and this token is not of a limited 
     * scope type such as a record component, enum literal, label, etc. */
    if((Option.include.qualifiedTags == TRUE) &&
       (token->kind != ADA_KIND_RECORD_COMPONENT) &&
       (token->kind != ADA_KIND_ENUM_LITERAL) &&
       (token->kind != ADA_KIND_FORMAL) &&
       (token->kind != ADA_KIND_LABEL) &&
       (token->kind != ADA_KIND_IDENTIFIER) &&
       (token->kind != ADA_KIND_AUTOMATIC_VARIABLE) &&
       (token->kind != ADA_KIND_ANONYMOUS))
    {
      if(parentScope != NULL)
      {
        /* first create our new scope which is the parent scope + '.' + the 
         * current tag name. */
        currentScope = xMalloc(strlen(parentScope) + strlen(token->name) + 2,
                               char);
        strncpy(currentScope, parentScope, strlen(parentScope));
        currentScope[strlen(parentScope)] = '.';
        strncpy(&currentScope[strlen(parentScope) + 1], token->name,
                strlen(token->name));
        currentScope[strlen(parentScope) + 1 + strlen(token->name)] = '\0';

        token->tag.name = currentScope;
        makeTagEntry(&token->tag);
      } /* if(parentScope != NULL) */
      else
      {
        /* if the parent scope is null then the current token does not have 
         * a parent tag to prepend onto the current scope.  Therefore, just 
         * setup the current scope as a copy of the current tag name, but make 
         * no extra entry. */
        currentScope = token->name;
      }
    } /* if((Option.include.qualifiedTags == TRUE) && ... */
  } /* if((token->kind > ADA_KIND_UNDEFINED) && ... */

  /* now make the child tags */
  tmp = token->children.head;
  while(tmp != NULL)
  {
    storeAdaTags(tmp, currentScope);
    tmp = tmp->next;
  }

  /* we have to clear out the declare name here or else it may cause issues
   * when we try to process it's children, and when we try to free the token
   * data */
  if(token->kind == ADA_KIND_ANONYMOUS &&
     strncasecmp(token->name, AdaKeywords[ADA_KEYWORD_DECLARE],
                 strlen(AdaKeywords[ADA_KEYWORD_DECLARE])) == 0)
  {
    token->name = NULL;
    token->tag.name = NULL;
  }

  if((currentScope != NULL) && (currentScope != token->name))
  {
    eFree((void *) currentScope);
  }
} /* static void storeAdaTags(adaTokenInfo *token, const char *parentScope) */

/* main parse function */
static void findAdaTags(void)
{
  adaTokenInfo root;
  adaTokenInfo *tmp;

  /* init all global data now */
  exception = EXCEPTION_NONE;
  line = NULL;
  pos = 0;
  matchLineNum = 0;
  eofCount = 0;

  /* cannot just set matchFilePos to 0 because the fpos_t is not a simple 
   * integer on all systems. */
  matchFilePos = getInputFilePosition();

  /* init the root tag */
  root.kind = ADA_KIND_UNDEFINED;
  root.isSpec = FALSE;
  root.name = NULL;
  root.parent = NULL;
  root.isPrivate = FALSE;
  initAdaTokenList(&root.children);

  /* read in the first line */
  readNewLine();
  if (exception == EXCEPTION_EOF)
    goto out;

  /* tokenize entire file */
  exception = setjmp(eofError);
  while(exception != EXCEPTION_EOF && adaParse(ADA_ROOT, &root) != NULL);

  /* store tags */
  tmp = root.children.head;
  while(tmp != NULL)
  {
    storeAdaTags(tmp, NULL);
    tmp = tmp->next;
  }

 out:
  /* clean up tokens */
  freeAdaTokenList(&root.children);
} /* static void findAdaTags(void) */

/* parser definition function */
extern parserDefinition* AdaParser(void)
{
  static const char *const extensions[] = { "adb", "ads", "Ada", NULL };
  parserDefinition* def = parserNew("Ada");
  def->kinds = AdaKinds;
  def->kindCount = ADA_KIND_COUNT;
  def->extensions = extensions;
  def->parser = findAdaTags;
  return def;
} /* extern parserDefinition* AdaParser(void) */
