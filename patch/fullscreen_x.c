void fullscreen(const Arg *arg)
{
  if (wl.fullscreen_state)
    xdg_toplevel_unset_fullscreen(wl.xdgtoplevel);
  else
    xdg_toplevel_set_fullscreen(wl.xdgtoplevel, NULL);
}
