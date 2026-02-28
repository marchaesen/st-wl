void
kscrolldown(const Arg* a)
{
	int n = a->i;

	if (n < 0)
		n = -n;

	if (n > term.scr)
		n = term.scr;

	if (term.scr > 0) {
		term.scr -= n;
		selscroll(0, -n);
		tfulldirt();
	}

	#if SIXEL_PATCH
	scroll_images(-1*n);
	#endif // SIXEL_PATCH

	#if OPENURLONCLICK_PATCH
	if (n > 0)
		restoremousecursor();
	#endif // OPENURLONCLICK_PATCH
}

/* Scroll down without clearing active selection */
void
kscrolldown_nosel(int n)
{
	if (n < 0)
		n = -n;

	if (n > term.scr)
		n = term.scr;

	if (term.scr > 0) {
		DBGSEL("scrolldown_nosel n=%d scr=%d->%d ob.y=%d oe.y=%d",
			n, term.scr, term.scr-n, sel.ob.y, sel.oe.y);
		term.scr -= n;
		sel.ob.y -= n;
		sel.oe.y -= n;
		sel.nb.y = MIN(sel.ob.y, sel.oe.y);
		sel.ne.y = MAX(sel.ob.y, sel.oe.y);
		DBGSEL("scrolldown_nosel after: ob.y=%d oe.y=%d nb.y=%d ne.y=%d",
			sel.ob.y, sel.oe.y, sel.nb.y, sel.ne.y);
		if ((sel.ob.y < 0 && sel.oe.y < 0) ||
		    (sel.ob.y >= term.row && sel.oe.y >= term.row))
			selclear();
		tfulldirt();
	}
}

void
kscrollup(const Arg* a)
{
	int n = a->i;
	if (n < 0)
		n = -n;

	if (term.scr + n > term.histn)
		n = term.histn - term.scr;

	if (!n)
		return;

	if (term.scr <= HISTSIZE-n) {
		term.scr += n;
		selscroll(0, n);
		tfulldirt();
	}

	#if SIXEL_PATCH
	scroll_images(n);
	#endif // SIXEL_PATCH

	#if OPENURLONCLICK_PATCH
	if (n > 0)
		restoremousecursor();
	#endif // OPENURLONCLICK_PATCH
}

/* Scroll up without clearing active selection */
void
kscrollup_nosel(int n)
{
	if (n < 0)
		n = -n;

	if (term.scr + n > term.histn)
		n = term.histn - term.scr;

	if (!n)
		return;

	if (term.scr <= HISTSIZE-n) {
		DBGSEL("scrollup_nosel n=%d scr=%d->%d ob.y=%d oe.y=%d",
			n, term.scr, term.scr+n, sel.ob.y, sel.oe.y);
		term.scr += n;
		sel.ob.y += n;
		sel.oe.y += n;
		sel.nb.y = MIN(sel.ob.y, sel.oe.y);
		sel.ne.y = MAX(sel.ob.y, sel.oe.y);
		DBGSEL("scrollup_nosel after: ob.y=%d oe.y=%d nb.y=%d ne.y=%d",
			sel.ob.y, sel.oe.y, sel.nb.y, sel.ne.y);
		if ((sel.ob.y < 0 && sel.oe.y < 0) ||
		    (sel.ob.y >= term.row && sel.oe.y >= term.row))
			selclear();
		tfulldirt();
	}
}