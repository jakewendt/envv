/***************************************************************/
/*                                                             */
/*  ENVV.C                                                     */
/*                                                             */
/*  Program to manipulate environment variables in a           */
/*  shell-independent way.                                     */
/*                                                             */
/*  Copyright (C) 1994 by David F. Skoll                       */
/*                                                             */
/*     986 Eiffel Avenue                                       */
/*     Ottawa, Ontario                                         */
/*     K2C 0J2 Canada                                          */
/*                                                             */
/*     (613) 225-8687                                          */
/*     <dfs@doe.carleton.ca>                                   */
/*     <aa775@freenet.carleton.ca>                             */
/*                                                             */
/*  Usage:                                                     */
/*  eval `envv choose sh-name csh-name`                        */
/*  eval `envv set ENVVAR value`                               */
/*  eval `envv local VAR value`                                */
/*  eval `envv add PATHVAR dir [position]`                     */
/*  eval `envv del PATHVAR dir`                                */
/*  eval `envv move PATHVAR dir position`                      */
/*                                                             */
/*  Options:                                                   */
/*   -e = don't escape shell chars                             */
/*   -s = put trailing semicolon after each command.           */
/*   -h = display usage information                            */
/*                                                             */
/*  If no commands given on command line, read from stdin      */
/*                                                             */
/***************************************************************/
#define VERSION "1.6"

/* I need to use malloc.  If its prototype is in malloc.h, #define
   NEED_MALLOC_H. */

/* #define NEED_MALLOC_H 1 */

/* If you don't have the function strcasecmp, #define NO_STRCASECMP */
/* #define NO_STRCASECMP */

/* If you have stdlib.h, define HAVE_STDLIB_H. */
#define HAVE_STDLIB_H 1

#define _POSIX_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>

#ifdef NEED_MALLOC_H
#include <malloc.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

/* Maximum number of components allowed in a colon-separated path list */

#define MAXCOMPONENTS 256

char *PathComp[MAXCOMPONENTS];
int NumComponents;

/* Does the C compiler have prototypes and const? */
#ifdef __STDC__
#define HAVE_PROTOS
#endif

#ifdef HAVE_PROTOS
#define ARGS(x) x
#else
#define ARGS(x) ()
#endif

/* Possible types of shells */
#define NO_SH    -1
#define SH_LIKE  0
#define CSH_LIKE 1

/* Possible directives */
#define NO_D  -1
#define D_SET  0
#define D_MOVE 1
#define D_ADD  2
#define D_DEL  3
#define D_CHOOSE 4
#define D_LOCAL 5

/* Positions */
#define NO_P  0

typedef struct {
   char *name;
   int type;
} ShellType;

ShellType Shells[] = {
   { "bash", SH_LIKE },
   { "sh",   SH_LIKE },
   { "ksh",  SH_LIKE },
   { "zsh",  SH_LIKE },
   { "rsh",  SH_LIKE },
   { "csh",  CSH_LIKE },
   { "tcsh", CSH_LIKE },
   { NULL,   NO_SH }
};

/* A list of all the characters which should be escaped */
char *escape = "\\\"'!$%^&*()[]<>{}`~| ;?";
static int ShouldEscape = 1;

/* Trailing semicolon? */
char *TrailingSemi = "\n";

/* Was command supplied on cmd. line? */
static int UseCmdLine;

/* Command-line arguments */
int Argc;
char **Argv;
int FirstArg;

/* Global vars for directives, args, etc. */
#define MAX_DIR_LEN 40
#define MAX_VAR_LEN 256
#define MAX_VAL_LEN 2048
#define MAX_POS_LEN 20

char Directive[MAX_DIR_LEN+1];
char Var[MAX_VAR_LEN+1];
char Val[MAX_VAL_LEN+1];
char Pos[MAX_POS_LEN+1];
int ArgsSupplied;

/* Function Prototypes */
void Init ARGS ((int argc, char *argv[]));
int FigureShellTypeFromName ARGS ((char *s));
int GetShellType ARGS ((void));
void DoSetenv ARGS ((const char *var, const char *val, int shell, int local));
void DoAdd ARGS ((const char *var, const char *val, int shell, int pos));
void DoDel ARGS ((const char *var, const char *val, int shell));
void DoMove ARGS ((const char *var, const char *val, int shell, int pos));
void DoChoose ARGS ((const char *val1, const char *val2, int shell));
int SplitPath ARGS ((char *path));
int FindCurPos ARGS ((const char *dir));
void PrintEscaped ARGS ((const char *s, int colon));
void PathManip ARGS ((const char *var, const char *dir, int shell, int pos, int what));
void Usage ARGS ((const char *name));
int ComparePathElements ARGS ((const char *p1, const char *p2));
int GetCommand ARGS ((void));
int ReadEscapedToken ARGS ((char *buf, int len, int eoln_flag));
int ReadCmdFromStdin ARGS ((void));

/***************************************************************/
/*                                                             */
/*  strcasecmp:  Define my own for those compilers that don't  */
/*  have it.                                                   */
/*                                                             */
/***************************************************************/
#ifdef NO_STRCASECMP
#ifdef HAVE_PROTOS
int strcasecmp(const char *s1, const char *s2)
#else
int strcasecmp(s1, s2)
char *s1, *s2;
#endif
{
   char c1, c2;
   while(*s1 && *s2) { /* Could use while(*s1), but it's more cryptic */
      c1 = isupper(*s1) ? *s1 : toupper(*s1);
      c2 = isupper(*s2) ? *s2 : toupper(*s2);
      if (c1 != c2) return c1-c2;
      s1++;
      s2++;
   }
   c1 = isupper(*s1) ? *s1 : toupper(*s1);
   c2 = isupper(*s2) ? *s2 : toupper(*s2);
   return c1-c2;
}

#endif

/***************************************************************/
/*                                                             */
/*  PrintEscaped                                               */
/*                                                             */
/*  Print a string, escaping shell chars.  A baroque set of    */
/*  params:  If colon is 0, reset internal flag.  If it's 1,   */
/*  and internal flag is reset, then set internal flag.  If it */
/*  is 1 and internal flag is set, print a colon before string */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
void PrintEscaped(const char *s, int colon)
#else
void PrintEscaped(s, colon)
char *s;
int colon;
#endif
{
   static int internal_flag = 0;

   if (!colon) internal_flag = 0;

   if (!s || !*s) return;

   if (colon) {
      if (internal_flag) putchar(':');
      else internal_flag = 1;
   }
   while (*s) {
      if (ShouldEscape && strchr(escape, *s)) putchar('\\');
      putchar(*s);
      s++;
   }
}

/***************************************************************/
/*                                                             */
/*  GetShellType                                               */
/*                                                             */
/*  Get the type of the user's shell.  First, look for SHELL   */
/*  environment variable.  If that doesn't work, use getpwuid  */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
int GetShellType(void)
#else
int GetShellType()
#endif
{
   char *s;
   int type;
   struct passwd *pw;

   s = getenv("SHELL");

   type = FigureShellTypeFromName(s);
   if (type != NO_SH) return type;

   /* Didn't work -- use getpwuid */
   pw = getpwuid(geteuid());
   if (pw)
      type = FigureShellTypeFromName(pw->pw_shell);

   return type;
}

/***************************************************************/
/*                                                             */
/*  FigureShellTypeFromName                                    */
/*                                                             */
/*  Given the name of a shell, figure out if it's like sh or   */
/*  csh.                                                       */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
int FigureShellTypeFromName(char *s)
#else
int FigureShellTypeFromName(s)
char *s;
#endif
{
   int i;
   char *t;

   if (!s) return NO_SH;

   /* Move past the last '/' in the shell name */
   for (t=s; *s; s++)
     if (*s == '/') t=s+1;

   /* Figure out the type of shell */
   for (i=0; Shells[i].name; i++)
     if (!strcmp(t, Shells[i].name)) return Shells[i].type;

   /* Didn't match anything */
   return NO_SH;
}

/***************************************************************/
/*                                                             */
/*  ComparePathElements                                        */
/*                                                             */
/*  Return 0 if two path elements are the same, non-zero       */
/*  otherwise.  Trailing slashes do not participate in the     */
/*  comparison.                                                */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
int ComparePathElements(const char *p1, const char *p2)
#else
int ComparePathElements(p1, p2)
char *p1;
char *p2;
#endif
{
   while (*p1) {
      if (*p1 != *p2) break;
      p1++;
      p2++;
   }

   /* Trailing slashes don't count */
   if ((*p1 != '/' && *p1 != 0) ||
       (*p2 != '/' && *p2 != 0)) {
      return (*p1 - *p2);
   }

   while(*p1 == '/') p1++;
   while(*p2 == '/') p2++;
   return (*p1 - *p2);

}

/***************************************************************/
/***************************************************************/
/***************************************************************/
/***                                                         ***/
/***                        MAIN PROGRAM                     ***/
/***                                                         ***/
/***************************************************************/
/***************************************************************/
/***************************************************************/
#ifdef HAVE_PROTOS
int main(int argc, char *argv[])
#else
int main(argc, argv)
int argc;
char *argv[];
#endif
{
   int what;
   int pos;
   int shell;

   Init(argc, argv);

   shell = GetShellType();
   if (shell == NO_SH) {
      fprintf(stderr, "%s: Can't figure out shell type!\n", argv[0]);
      return 1;
   }

   while(1) {
      what = NO_D;
      pos = NO_P;
      if (!GetCommand()) return 0;
      if (ArgsSupplied < 3) {
	 if (UseCmdLine) {
	    Usage(Argv[0]);
	    return 1;
	 } else {
	    fprintf(stderr, "%s: not enough arguments in command\n", Argv[0]);
	 }
	 continue;
      }
      if      (!strcasecmp(Directive, "set"))    what = D_SET;
      else if (!strcasecmp(Directive, "add"))    what = D_ADD;
      else if (!strcasecmp(Directive, "del"))    what = D_DEL;
      else if (!strcasecmp(Directive, "move"))   what = D_MOVE;
      else if (!strcasecmp(Directive, "choose")) what = D_CHOOSE;
      else if (!strcasecmp(Directive, "local"))  what = D_LOCAL;

      if (what == NO_D) {
	 if (UseCmdLine) {
	    Usage(argv[0]);
	    return 1;
	 } else {
	    fprintf(stderr, "%s: unknown directive %s\n", Argv[0], Directive);
	    continue;
	 }
      }

      if (ArgsSupplied >= 4) pos = atoi(Pos);

      switch(what) {
       case D_SET:  DoSetenv(Var, Val, shell, 0); break;
       case D_LOCAL: DoSetenv(Var, Val, shell, 1); break;
       case D_CHOOSE: DoChoose(Var, Val, shell); break;
       case D_ADD:
       case D_DEL:
       case D_MOVE: PathManip(Var, Val, shell, pos, what); break;
       default: fprintf(stderr, "%s: internal error - unknown directive %d\n",
			Argv[0], what);
      }
   }
   return 0;
}

/***************************************************************/
/*                                                             */
/*  DoSetenv                                                   */
/*                                                             */
/*  Issue the command to set an environment variable.          */
/*  Escape shell characters that may cause problems.           */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
void DoSetenv(const char *var, const char *val, int shell, int local)
#else
void DoSetenv(var, val, shell, local)
char *var;
char *val;
int shell;
int local;
#endif
{
   char *envstr;

   switch(shell) {
    case SH_LIKE:
      printf("%s=", var);
      PrintEscaped(val, 0);
      if (!local)
	printf("; export %s", var);
      printf(TrailingSemi);
      break;

    case CSH_LIKE:
      if (!local)
	printf("setenv %s ", var);
      else
	printf("set %s=", var);

      PrintEscaped(val, 0);
      printf(TrailingSemi);
      break;

    default:
      fprintf(stderr, "%s: internal error - bad shell value %d\n", Argv[0], shell);
   }

   /* If not reading from cmd line, set the value in the environment */
   if (!UseCmdLine) {
      envstr = malloc(strlen(var)+strlen(val)+2);
      if (envstr) {
	 sprintf(envstr, "%s=%s", var, val);
	 putenv(envstr);
      } else {
	 fprintf(stderr, "%s: out of memory!\n", Argv[0]);
	 exit(1);
      }
   }
      
   return;
}

/***************************************************************/
/*                                                             */
/*  SplitPath                                                  */
/*                                                             */
/*  Split a colon-separated path list into its components      */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
int SplitPath(char *path)
#else
int SplitPath(path)
char *path;
#endif
{
   NumComponents = 0;
   if(!path) return 0;

/* Strip leading colons, if any */
   while(*path == ':') path++;
   if (!*path) return 0;

   NumComponents = 1;
   PathComp[0] = path;
   while(*path) {
      if (*path == ':') {
	 *path++ = 0;
	 while (*path == ':') path++;
	 if (*path) {
	    PathComp[NumComponents] = path;
	    NumComponents++;
	    if(NumComponents == MAXCOMPONENTS) return NumComponents;
         }
      }
      path++;
   }
   return NumComponents;
}

/***************************************************************/
/*                                                             */
/*  FindCurPos                                                 */
/*                                                             */
/*  Find the current position of a dir in current split path.  */
/*  Return 0 if not in current path.                           */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
int FindCurPos(const char *dir)
#else
int FindCurPos(dir)
char *dir;
#endif
{
   int i;
   for (i=0; i<NumComponents; i++)
     if (!ComparePathElements(dir, PathComp[i])) return i+1;

   return NO_P;
}

/***************************************************************/
/*                                                             */
/*  PathManip                                                  */
/*                                                             */
/*  Manipulate the components of a path.                       */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
void PathManip(const char *var, const char *dir, int shell, int pos, int what)
#else
void PathManip(var, dir, shell, pos, what)
char *var;
char *dir;
int shell;
int pos;
int what;
#endif
{
   int curpos;
   char *path;
   int i, j;
   char *envstr = NULL;
   char *s;
   int oldpathlen;
   int newpathlen;

   /* Get current value of var */
   path = getenv(var);

   if(path) {
      oldpathlen = strlen(path);
      path=(char *) strdup(path); /* Our brain-dead system doesn't have a
				     proper prototype for strdup */
      if (!path) {
	 fprintf(stderr, "%s: out of memory!\n", Argv[0]);
	 exit(1);
      }
   } else {
      oldpathlen = 0;
   }

   /* Split the path into its components */
   (void) SplitPath(path);

   /* Find current pos of dir */
   curpos = FindCurPos(dir);

   /* If it's 'add' and dir already exists in path, do nothing if pos
      not specified.  If pos specified, OR trailing slashes
      don't match, convert to 'move' */
   if (what == D_ADD && curpos != NO_P) {
      if (pos == NO_P && strcmp(dir, PathComp[curpos-1])) {
	 pos = curpos;
      }
      if (pos == NO_P) {
	 free(path);
	 return;
      }
      what = D_MOVE;
   }

   /* If it's 'del' or 'move' and dir does not exist in path, do nothing */
   if ((what == D_MOVE || what == D_DEL) && curpos == NO_P) {
      free(path);
      return;
   }
   if (pos == NO_P) {
      if (what == D_MOVE) {
	 fprintf(stderr, "%s: position must be supplied for 'move'\n", Argv[0]);
	 free(path);
	 return;
      }
      pos = curpos;
   }

   /* If we are taking input from stdin, we must modify our environment
      to reflect the updated path, so that multiple ADD, DEL, etc.
      commands work properly.  If we don't do this, only the last
      path manipulation command has any effect, since PathManip re-reads
      the value from the environment each time it is called. */

   if (!UseCmdLine) {
      /* Max. length is: Length of var name + '=' + oldpath + ':' + component
	 + '\0' */
/*
      newpathlen = oldpathlen + 3 + strlen(var) + strlen(dir); 

	github.com issue #1 raised by bukzor with fix from David Skoll 

	The code is wrong on all platforms (it overwrites one byte past the
	malloc'd string with a 0. I guess it only manifested itself on x86_64
	because of details of the glibc implementation on that platform.

	Changing the 3 to a 4.

*/
      newpathlen = oldpathlen + 4 + strlen(var) + strlen(dir);
      envstr = (char *) malloc(newpathlen);
      if (!envstr) {
	 fprintf(stderr, "%s: out of memory!\n", Argv[0]);
	 exit(1);
      }
      sprintf(envstr, "%s=", var);
      s = envstr + strlen(envstr);
   }
   /* Print the path components */
   switch(shell) {
    case SH_LIKE: printf("%s=", var); break;
    case CSH_LIKE: printf("setenv %s ", var); break;
   }

   /* Reset colon flag */
   PrintEscaped(NULL, 0);

   /* Do it! */
   for (i=0; i<NumComponents; i++) {
      j = i+1;
      switch(what) {
       case D_DEL:
	 if (curpos != j) {
	    PrintEscaped(PathComp[i],1);
	    if (!UseCmdLine) {
	       sprintf(s, "%s:", PathComp[i]);
	       s += strlen(s);
	    }
	 }
	 break;

       case D_MOVE:
	 if (pos < curpos) {
	    if (pos == j) {
	       PrintEscaped(dir, 1);
	       if (!UseCmdLine) {
		  sprintf(s, "%s:", dir);
		  s += strlen(s);
	       }
	    }
	    if (j != curpos) {
	       PrintEscaped(PathComp[i],1);
	       if (!UseCmdLine) {
		  sprintf(s, "%s:", PathComp[i]);
		  s += strlen(s);
	       }
	    }
         } else {
	    if (j != curpos) {
	       PrintEscaped(PathComp[i],1);
	       if (!UseCmdLine) {
		  sprintf(s, "%s:", PathComp[i]);
		  s += strlen(s);
	       }
	    }
	    if (pos == j) {
	       PrintEscaped(dir, 1);
	       if (!UseCmdLine) {
		  sprintf(s, "%s:", dir);
		  s += strlen(s);
	       }
	    }
	 }
	 break;

       case D_ADD:
	 if (pos == j) {
	    PrintEscaped(dir, 1);
	    if (!UseCmdLine) {
	       sprintf(s, "%s:", dir);
	       s += strlen(s);
	    }
	 }
	 PrintEscaped(PathComp[i], 1);
	 if (!UseCmdLine) {
	    sprintf(s, "%s:", PathComp[i]);
	    s += strlen(s);
	 }
      }
   }

   /* Check ADD with no pos, or pos out of range */
   if ((what == D_ADD || what == D_MOVE) && (pos < 1 || pos > NumComponents)) {
      PrintEscaped(dir, 1);
      if (!UseCmdLine) {
	 sprintf(s, "%s:", dir);
	 s += strlen(s);
      }
   }

   /* If reading from a file, chew off the final colon in the new path
      and put it in environment. */
   if (!UseCmdLine) {
      *--s = 0;
      putenv(envstr);
   }


   switch(shell) {
    case SH_LIKE: printf("; export %s%s", var, TrailingSemi); break;
    case CSH_LIKE: printf(TrailingSemi); break;
   }
   free(path);
   return;
}

/***************************************************************/
/*                                                             */
/*  DoChoose                                                   */
/*                                                             */
/*  Simple-minded:  If shell is 0, print val1, else print val2 */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
void DoChoose(const char *val1, const char *val2, int shell)
#else
void DoChoose(val1, val2, shell)
char *val1;
char *val2;
int shell;
#endif
{
   switch(shell) {
      case SH_LIKE:
         PrintEscaped(val1, 0);
	 printf(TrailingSemi);
	 break;

     case CSH_LIKE:
	 PrintEscaped(val2, 0);
	 printf(TrailingSemi);
	 break;

     default:
	 fprintf(stderr, "%s: DoChoose - bad value for shell %d\n", Argv[0], shell);
	 break;
   }
}


/***************************************************************/
/*                                                             */
/*  Usage - print usage instructions                           */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
void Usage(const char *name)
#else
void Usage(name)
char *name;
#endif
{
   fprintf(stderr, "%s (version %s) Copyright 1994 by David F. Skoll\n\n",
	   name, VERSION);
   fprintf(stderr, "Usage:\n");
   fprintf(stderr, "   %s [options] set var value\n", name);
   fprintf(stderr, "   %s [options] local var value\n", name);
   fprintf(stderr, "   %s [options] add pathvar dir [pos]\n", name);
   fprintf(stderr, "   %s [options] move pathvar dir pos\n", name);
   fprintf(stderr, "   %s [options] del pathvar dir\n", name);
   fprintf(stderr, "   %s [options] choose sh_choice csh_choice\n", name);
   fprintf(stderr, "\nOptions:\n");
   fprintf(stderr, "   -e = Do not escape shell meta-characters\n");
   fprintf(stderr, "   -s = Put trailing semicolon after each command\n");
   fprintf(stderr, "   -h = Display usage information\n");
   fprintf(stderr, "\nIf no directives are given on command line, they\n");
   fprintf(stderr, "are read from stdin.  Multiple directives may be\n");
   fprintf(stderr, "issued this way.\n");
   exit(1);
}

/***************************************************************/
/*                                                             */
/* Init                                                        */
/*                                                             */
/* Read command-line args and options.                         */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
void Init(int argc, char *argv[])
#else
void Init(argc, argv)
int argc;
char *argv[];
#endif
{
   int i;
   char *s;

   /* Set global vars */
   Argc = argc;
   Argv = argv;

   /* Get the options */
   for (i=1; i<argc; i++) {
      if (*argv[i] != '-') break;
      s = argv[i]+1;
      while(*s) {
	 switch (*s) {
	  case 'e':
	  case 'E':
	    ShouldEscape = 0;
	    break;

	  case 'h':
	  case 'H':
	    Usage(argv[0]);
	    exit(1);

	  case 's':
	  case 'S':
	    TrailingSemi = " ;\n";
	    break;

	  default:
	    fprintf(stderr, "%s: Unknown option '%c'\n", argv[0], *s);
	    break;
	 }
	 s++;
      }
   }

   /* 'i' holds index of first argument. */
   FirstArg = i;
   if (FirstArg < argc) UseCmdLine = 1;
   else UseCmdLine = 0;
}

/***************************************************************/
/*                                                             */
/*  GetCommand                                                 */
/*                                                             */
/*  Get a command, either from stdin or the command line.      */
/*  Return 1 for success, 0 for failure.                       */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
int GetCommand(void)
#else
int GetCommand()
#endif
{
   ArgsSupplied = 0;

   if (UseCmdLine == 1) {
      UseCmdLine = 2;
      if (Argc > FirstArg) {
	 strncpy(Directive, Argv[FirstArg], MAX_DIR_LEN);
	 Directive[MAX_DIR_LEN] = 0;
	 ArgsSupplied++;
      }
      if (Argc > FirstArg+1) {
	 strncpy(Var, Argv[FirstArg+1], MAX_VAR_LEN);
	 Var[MAX_VAR_LEN] = 0;
	 ArgsSupplied++;
      }
      if (Argc > FirstArg+2) {
	 strncpy(Val, Argv[FirstArg+2], MAX_VAL_LEN);
	 Val[MAX_VAL_LEN] = 0;
	 ArgsSupplied++;
      }
      if (Argc > FirstArg+3) {
	 strncpy(Pos, Argv[FirstArg+3], MAX_POS_LEN);
	 Pos[MAX_POS_LEN] = 0;
	 ArgsSupplied++;
      }
      return 1;
   } else if (UseCmdLine > 1) {
      return 0;
   } else {
      return ReadCmdFromStdin();
   }
}

/***************************************************************/
/*                                                             */
/* ReadCmdFromStdin                                            */
/*                                                             */
/* Read a command from stdin, handling backslash-escaped chars */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
int ReadCmdFromStdin(void)
#else
int ReadCmdFromStdin()
#endif
{
   /* Try reading the directive first */
   if (!ReadEscapedToken(Directive, MAX_DIR_LEN, 0)) return 0;

   ArgsSupplied = 1;

   /* Read var, value and pos */
   if (ReadEscapedToken(Var, MAX_VAR_LEN, 1)) {
      ArgsSupplied++;
      if (ReadEscapedToken(Val, MAX_VAL_LEN, 1)) {
	 ArgsSupplied++;
	 if (ReadEscapedToken(Pos, MAX_POS_LEN, 1)) ArgsSupplied++;
      }
   }
   return 1;
}

/***************************************************************/
/*                                                             */
/* ReadEscapedToken                                            */
/*                                                             */
/* Read a token from stdin.                                    */
/*                                                             */
/***************************************************************/
#ifdef HAVE_PROTOS
int ReadEscapedToken(char *buf, int len, int eoln_flag)
#else
int ReadEscapedToken(buf, len, eoln_flag)
char *buf;
int len;
int eoln_flag;
#endif
{
   static int seen_eoln = 0;
   int nread = 0;
   char ch;

   if (!eoln_flag) seen_eoln = 0;

/* Reached the end of current line -- return 0 */
   if (seen_eoln) return 0;

/* Skip whitespace */
   ch = getchar();
   if (ch == EOF) return 0;
   while (isspace(ch)) {
      if (ch == '\n' && eoln_flag) {
	 seen_eoln = 1;
	 return 0;
      }
      ch = getchar();
   }

/* Read 'len' escaped chars */
   while (nread < len) {
      if (ch == EOF) return 0; /* EOF reached */
      if (ch == '\\') {
	 ch = getchar();
	 if (ch == EOF) return 0;
	 *buf++ = ch;
	 nread++;
	 ch = getchar();
	 continue;
      } else if (isspace(ch)) break;
      else {
	 *buf++ = ch;
	 nread++;
	 ch = getchar();
      }
   }

/* If we didn't halt because of whitespace, skip to next whitespace */
   while (ch != EOF && !isspace(ch)) ch = getchar();

   if (ch == '\n') seen_eoln = 1;
   *buf = 0;
   return 1;
}
