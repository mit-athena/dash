/*
 * $Id: klist.c,v 1.4 2004-03-11 20:37:56 rbasch Exp $
 *
 * Copyright 1990, 1991 by the Massachusetts Institute of Technology. 
 *
 * For copying and distribution information, please see the file
 * <mit-copyright.h>. 
 *
 */

#if  (!defined(lint))  &&  (!defined(SABER))
static char *rcsid =
"$Id: klist.c,v 1.4 2004-03-11 20:37:56 rbasch Exp $";
#endif

#include "mit-copyright.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <krb.h>
#include <Jets/Jets.h>
#include <Jets/Button.h>
#include <Jets/warn.h>


#ifndef TICKET_GRANTING_TICKET
#define TICKET_GRANTING_TICKET	"krbtgt"
#endif

#define WARN1_TIME 15
#define WARN2_TIME 5

extern Jet root;

static Warning *old_warn = NULL;
static int ok();

void checkTkts(info, id)
     int info, id;
{
  char pname[ANAME_SZ];
  char pinst[INST_SZ];
  char prealm[REALM_SZ];
  int k_errno;
  CREDENTIALS c;
  char *file;
  int ret = 0;
  static int old_ret = 0;
  static time_t mod_time = 0;
  static long exp_time;
  struct stat statbuf;
  int diff;
  unsigned int timeout = 5*60*1000;	/* 5 minutes... */
  char line1[100];
  char *line2 = "Type `renew' to re-authenticate.";

  line1[0] = '\0';

  if ((file = getenv("KRBTKFILE")) == NULL)
    file = TKT_FILE;


  if (stat(file, &statbuf))
    {
      sprintf(line1, "%s: `%s'", strerror(errno), file);

      ret = 1;
      goto done;
    }

  if (statbuf.st_mtime != mod_time)
    mod_time = statbuf.st_mtime;
  else  if (exp_time - time(0) > 15 * 60)
    {
      ret = 0;
      goto done;
    }

  /* 
   * Since krb_get_tf_realm will return a ticket_file error, 
   * we will call tf_init and tf_close first to filter out
   * things like no ticket file.  Otherwise, the error that 
   * the user would see would be 
   * klist: can't find realm of ticket file: No ticket file (tf_util)
   * instead of
   * klist: No ticket file (tf_util)
   */

  /* Open ticket file */
  if (k_errno = tf_init(file, R_TKT_FIL))
    {
      sprintf(line1, "%s", krb_err_txt[k_errno]);
      ret = 1;
      goto done;
    }
  /* Close ticket file */
  (void) tf_close();

  /* 
   * We must find the realm of the ticket file here before calling
   * tf_init because since the realm of the ticket file is not
   * really stored in the principal section of the file, the
   * routine we use must itself call tf_init and tf_close.
   */
  if ((k_errno = krb_get_tf_realm(file, prealm)) != KSUCCESS)
    {
      sprintf(line1, "can't find realm of ticket file: %s",
	      krb_err_txt[k_errno]);
      ret = 1;
      goto done;
    }

  /* Open ticket file, get principal name and instance */
  if ((k_errno = tf_init(file, R_TKT_FIL)) ||
      (k_errno = tf_get_pname(pname)) ||
      (k_errno = tf_get_pinst(pinst)))
    {
      sprintf(line1, "%s", krb_err_txt[k_errno]);
      ret = 1;
      goto done;
    }

  /* 
   * You may think that this is the obvious place to get the
   * realm of the ticket file, but it can't be done here as the
   * routine to do this must open the ticket file.  This is why 
   * it was done before tf_init.
   */
       
  while ((k_errno = tf_get_cred(&c)) == KSUCCESS)
    {
      if (!strcmp(c.service, TICKET_GRANTING_TICKET) &&
	  !strcmp(c.instance, prealm))
	{
	  exp_time = c.issue_date + ((unsigned char) c.lifetime) * 5 * 60;
	  diff = exp_time - time(0);

	  if (diff < 0)
	    {
	      strcpy(line1, "Your authentication has expired.");
	      ret = 3;			/* has expired */
	      goto done;
	    }

	  if (diff < WARN1_TIME * 60)	/* inside of 15 minutes? */
	    {
	      char *expire_str =
		"Your authentication will expire in less than %d minutes.";
	      timeout = 60*1000;	/* set timeout to 1 minute... */

	      if (diff < WARN2_TIME * 60)	/* inside of 5 minutes? */
		{
		  sprintf(line1, expire_str, WARN2_TIME);
		  ret = 2;
		  goto done;
		}
	      sprintf(line1, expire_str, WARN1_TIME);
	      ret = 1;
	      goto done;
	    }

	  ret = 0;			/* tgt hasn't expired */
	  goto done;
	}
      continue;			/* not a tgt */
    }

  strcpy(line1, "You have no authentication.");
  ret = 1;			/* no tgt found */



 done:
  if (ret  &&  (old_ret != ret))
    {
      Warning *w;

      /* Destroy last warning if user hasn't clicked it away already */
      if (old_warn != NULL)
	XjCallCallbacks((caddr_t) old_warn,
			old_warn->button->button.activateProc, NULL);

      w = (Warning *)XjMalloc((unsigned) sizeof(Warning));

      w->me.next = NULL;
      w->me.argType = argPtr;
      w->me.passPtr = w;
      w->me.proc = ok;

      w->l1 = XjNewString(line1);
      w->l2 = XjNewString(line2);

      old_warn = XjUserWarning(root, w, True, line1, line2);
    }
  old_ret = ret;
  (void) tf_close();
  XjAddWakeup(checkTkts, NULL, timeout);
  /*  return ret;  */
}

static int ok(who, w, data)
     Jet who;
     Warning *w;
     caddr_t data;
{
  Display *dpy;

  dpy = w->top->core.display;	/* save off the display before */
				/* destroying the Jet */
  XjDestroyJet(w->top);
  XFlush(dpy);

  XjFree(w->l1);
  XjFree(w->l2);
  XjFree((char *) w);
  old_warn = NULL;
  return 0;
}
