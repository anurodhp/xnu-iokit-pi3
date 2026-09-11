/* DAR-286: real, on-target verification of the libedit contract that
 * scutil(8)'s interactive command loop actually depends on.
 *
 * WHY THIS EXISTS, AND WHAT "VERIFIED" HONESTLY MEANS HERE
 * =======================================================
 * libedit is a terminal line editor. The thing a human gets out of it --
 * arrow-key editing, ^R history search, emacs/vi keybindings -- needs
 * synthesised keypresses on a real tty to observe, and this project's
 * QEMU harness (tools/boot_test.py --cmd) can only send whole lines to a
 * shell. So "interactive editing verified" is NOT a claim this binary
 * can make, and it does not make it.
 *
 * What it does verify for real, on the target, is the exact contract
 * scutil.c is written against -- and specifically the contract that
 * scutil_editline_compat.c (the no-op stand-in this ticket removes)
 * deliberately FAILED. That stand-in made el_init()/history_init()
 * return NULL and el_get() return -1, which drove scutil.c into its own
 * Apple-authored teardown branch:
 *
 *	if ((el_get(src->el, EL_EDITMODE, &editmode) != -1) && editmode != 0) {
 *		el_set(src->el, EL_SIGNAL, 1);
 *	} else {
 *		history_end(src->h); src->h  = NULL;
 *		el_end(src->el);     src->el = NULL;
 *	}
 *
 * i.e. it always threw the editor away and fell back to fgets(). The
 * difference between "libedit is real" and "libedit is a stand-in" is
 * therefore fully observable without a single keypress: which side of
 * that `if` runs. This binary replays scutil.c's real setup sequence, in
 * scutil.c's real order, with scutil.c's real arguments, and reports
 * which branch a real scutil would now take.
 *
 * It then does one thing scutil's own code cannot be asked to do from a
 * boot log: it points libedit at a real file of its own and calls
 * el_gets() for real, proving the reader actually returns the line's
 * bytes. That exercises el.c/read.c/chared.c/history.c/terminal.c/tty.c
 * for real on the target rather than just proving the symbols resolved.
 *
 * WHAT A REAL RUN SHOWS, INCLUDING ONE REAL REMAINING GAP
 * =======================================================
 * Real QEMU boot, 2026-09-11 (qemu/dar286_libedit4.log):
 *
 *	libedit_smoketest: isatty(stdin)=1
 *	libedit_smoketest: history_init() ok
 *	libedit_smoketest: history(H_SETSIZE) rc=0
 *	Cannot read termcap database;
 *	using dumb terminal settings.
 *	libedit_smoketest: el_init() ok
 *	libedit_smoketest: el_set(EL_HIST) rc=0
 *	libedit_smoketest: el_set(EL_EDITOR,"emacs") rc=0
 *	libedit_smoketest: el_set(EL_PROMPT) rc=0
 *	libedit_smoketest: el_source(NULL) rc=-1
 *	libedit_smoketest: el_get(EL_EDITMODE) rc=0 editmode=1
 *	libedit_smoketest: scutil would KEEP the editor ...
 *	libedit_smoketest: el_gets() ok, count=31, read back the real line
 *	libedit_smoketest: history(H_ENTER) rc=1
 *	libedit_smoketest: PASS
 *
 * Two of those lines are real information, not noise, and neither is
 * papered over:
 *
 *   - `el_source(NULL) rc=-1` is correct and expected: there is no
 *     ~/.editrc on this image, and real libedit returns -1 for that.
 *     scutil ignores el_source's return value, exactly as Apple wrote it.
 *
 *   - `Cannot read termcap database; using dumb terminal settings.` is
 *     real libedit's own terminal.c reporting that tgetent("vt220")
 *     failed. That is a REAL, PRE-EXISTING IMAGE GAP, not something this
 *     library port introduced: there is no terminfo or termcap database
 *     anywhere on the SD image (confirmed by a real find over the
 *     mounted root, and inject_into_sd_image.sh has never mentioned
 *     one). libedit's own real fallback then runs, which is why the
 *     editor still comes up with editmode=1 and el_gets() works. What is
 *     NOT yet available is cursor-motion capability, so a human pressing
 *     an arrow key will not get real cursor movement until a terminfo
 *     database is installed -- filed as DAR-293 rather than claimed
 *     as working here.
 *
 * WHY THE INPUT IS A FILE AND NOT A TTY
 * =====================================
 * Run as a normal command from the root shell, this process's stdin IS
 * the console tty, so step 1 below genuinely exercises the tty/terminal
 * path (tty_init/terminal_init/tcgetattr/TERM lookup through real
 * libncurses). Step 2 deliberately uses a plain file instead, because a
 * second editor reading the same console would consume the shell's own
 * input. Both are real libedit; only step 2 is deterministic enough to
 * assert on, which is why the PASS line is gated on step 2's bytes and
 * step 1's editmode, and nothing else.
 */
#include <histedit.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *
smoketest_prompt(EditLine *el __attribute__((unused)))
{
	return (char *)"smoketest> ";
}

int
main(void)
{
	EditLine	*el;
	History		*h;
	HistEvent	ev;
	int		 editmode = 1;
	int		 rc;
	int		 failures = 0;

	printf("libedit_smoketest: start\n");

	/* ---------------------------------------------------------------
	 * Step 1: scutil.c's real setup sequence, in its real order, on
	 * this process's real stdin (a tty when run from the root shell).
	 * Every call and every argument below is copied from
	 * third_party/configd/scutil.tproj/scutil.c's own input-source
	 * setup, not invented.
	 * --------------------------------------------------------------- */
	printf("libedit_smoketest: isatty(stdin)=%d\n", isatty(fileno(stdin)));

	printf("libedit_smoketest: calling history_init()...\n");
	h = history_init();
	if (h == NULL) {
		printf("libedit_smoketest: history_init() returned NULL"
		       " -- this is what the old stand-in did\n");
		failures++;
	} else {
		printf("libedit_smoketest: history_init() ok\n");
		rc = history(h, &ev, H_SETSIZE, INT_MAX);
		printf("libedit_smoketest: history(H_SETSIZE) rc=%d\n", rc);
	}

	printf("libedit_smoketest: calling el_init()...\n");
	el = el_init("libedit_smoketest", stdin, stdout, stderr);
	if (el == NULL) {
		printf("libedit_smoketest: el_init() returned NULL"
		       " -- this is what the old stand-in did\n");
		failures++;
	} else {
		printf("libedit_smoketest: el_init() ok\n");
		if (h != NULL) {
			printf("libedit_smoketest: el_set(EL_HIST) rc=%d\n",
			       el_set(el, EL_HIST, history, h));
		}
		printf("libedit_smoketest: el_set(EL_EDITOR,\"emacs\") rc=%d\n",
		       el_set(el, EL_EDITOR, "emacs"));
		printf("libedit_smoketest: el_set(EL_PROMPT) rc=%d\n",
		       el_set(el, EL_PROMPT, smoketest_prompt));
		printf("libedit_smoketest: el_source(NULL) rc=%d\n",
		       el_source(el, NULL));

		rc = el_get(el, EL_EDITMODE, &editmode);
		printf("libedit_smoketest: el_get(EL_EDITMODE) rc=%d editmode=%d\n",
		       rc, editmode);
		if (rc != -1 && editmode != 0) {
			printf("libedit_smoketest: scutil would KEEP the editor"
			       " (el_set(EL_SIGNAL,1) branch) -- real line editing\n");
			printf("libedit_smoketest: el_set(EL_SIGNAL,1) rc=%d\n",
			       el_set(el, EL_SIGNAL, 1));
		} else {
			printf("libedit_smoketest: scutil would TEAR DOWN the editor"
			       " and fall back to fgets()\n");
			failures++;
		}
	}

	if (el != NULL) {
		el_end(el);
	}
	if (h != NULL) {
		history_end(h);
	}

	/* ---------------------------------------------------------------
	 * Step 2: a real el_gets() read, from a real file this process
	 * writes itself, so the assertion is deterministic and does not
	 * steal the console's input.
	 * --------------------------------------------------------------- */
	{
		static const char	 line[] = "show State:/Network/Global/DNS\n";
		const char		*path = "/tmp/libedit_smoketest.in";
		FILE			*fp;
		const char		*got;
		int			 count = -1;

		fp = fopen(path, "w");
		if (fp == NULL) {
			printf("libedit_smoketest: fopen(%s, \"w\") failed"
			       " -- cannot run the el_gets() check\n", path);
			failures++;
			goto done;
		}
		fwrite(line, 1, sizeof(line) - 1, fp);
		fclose(fp);

		fp = fopen(path, "r");
		if (fp == NULL) {
			printf("libedit_smoketest: fopen(%s, \"r\") failed\n", path);
			failures++;
			goto done;
		}

		printf("libedit_smoketest: calling el_init() on a real file...\n");
		el = el_init("libedit_smoketest", fp, stdout, stderr);
		if (el == NULL) {
			printf("libedit_smoketest: el_init() on a file returned NULL\n");
			failures++;
			fclose(fp);
			goto done;
		}
		h = history_init();
		if (h != NULL) {
			(void)history(h, &ev, H_SETSIZE, INT_MAX);
			(void)el_set(el, EL_HIST, history, h);
		}
		(void)el_set(el, EL_EDITOR, "emacs");
		(void)el_set(el, EL_PROMPT, smoketest_prompt);

		printf("libedit_smoketest: calling el_gets()...\n");
		got = el_gets(el, &count);
		if (got == NULL) {
			printf("libedit_smoketest: el_gets() returned NULL (count=%d)\n",
			       count);
			failures++;
		} else if (strncmp(got, line, sizeof(line) - 1) != 0) {
			printf("libedit_smoketest: el_gets() returned unexpected bytes:"
			       " count=%d got=\"%.*s\"\n", count, count, got);
			failures++;
		} else {
			printf("libedit_smoketest: el_gets() ok, count=%d, read back"
			       " the real line\n", count);
			/* Real H_ENTER, the other history call scutil makes
			 * (scutil.c's own getLine() does this for every
			 * non-empty line it reads). */
			if (h != NULL) {
				rc = history(h, &ev, H_ENTER, got);
				printf("libedit_smoketest: history(H_ENTER) rc=%d\n", rc);
			}
		}

		el_end(el);
		if (h != NULL) {
			history_end(h);
		}
		fclose(fp);
		(void)unlink(path);
	}

done:
	if (failures == 0) {
		printf("libedit_smoketest: PASS -- real libedit, real el_gets(),"
		       " scutil keeps the editor\n");
		return 0;
	}
	printf("libedit_smoketest: FAIL -- %d check(s) failed\n", failures);
	return 1;
}
