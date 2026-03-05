#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Form.h>
#include <stdio.h>
#include <stdlib.h>

static int click_count = 0;
static Widget count_label;

static void update_label(void) {
  char buf[64];
  snprintf(buf, sizeof(buf), "Clicked %d time%s", click_count,
           click_count == 1 ? "" : "s");
  XtVaSetValues(count_label, XtNlabel, buf, NULL);
}

static void on_click(Widget w, XtPointer client, XtPointer call) {
  click_count++;
  update_label();
}

static void on_reset(Widget w, XtPointer client, XtPointer call) {
  click_count = 0;
  update_label();
}

static void on_quit(Widget w, XtPointer client, XtPointer call) {
  exit(0);
}

int main(int argc, char *argv[]) {
  setvbuf(stdout, NULL, _IONBF, 0);

  XtAppContext app;
  Widget toplevel = XtVaAppInitialize(&app, "XawDemo", NULL, 0,
                                       &argc, argv, NULL, NULL);

  Widget form = XtVaCreateManagedWidget("form", formWidgetClass, toplevel,
                                         XtNdefaultDistance, 8, NULL);

  Widget title = XtVaCreateManagedWidget("title", labelWidgetClass, form,
                                          XtNlabel, "Athena Widget Demo",
                                          XtNborderWidth, 0,
                                          XtNtop, XawChainTop,
                                          XtNleft, XawChainLeft, NULL);

  count_label = XtVaCreateManagedWidget("count", labelWidgetClass, form,
                                         XtNlabel, "Clicked 0 times",
                                         XtNwidth, 200,
                                         XtNfromVert, title,
                                         XtNborderWidth, 0,
                                         XtNtop, XawChainTop,
                                         XtNleft, XawChainLeft, NULL);

  Widget click_btn = XtVaCreateManagedWidget("click", commandWidgetClass, form,
                                              XtNlabel, "Click Me",
                                              XtNwidth, 90,
                                              XtNfromVert, count_label,
                                              XtNtop, XawChainTop,
                                              XtNleft, XawChainLeft, NULL);
  XtAddCallback(click_btn, XtNcallback, on_click, NULL);

  Widget reset_btn = XtVaCreateManagedWidget("reset", commandWidgetClass, form,
                                              XtNlabel, "Reset",
                                              XtNwidth, 90,
                                              XtNfromVert, count_label,
                                              XtNfromHoriz, click_btn,
                                              XtNtop, XawChainTop,
                                              XtNleft, XawChainLeft, NULL);
  XtAddCallback(reset_btn, XtNcallback, on_reset, NULL);

  Widget quit_btn = XtVaCreateManagedWidget("quit", commandWidgetClass, form,
                                             XtNlabel, "Quit",
                                             XtNwidth, 90,
                                             XtNfromVert, count_label,
                                             XtNfromHoriz, reset_btn,
                                             XtNtop, XawChainTop,
                                             XtNleft, XawChainLeft, NULL);
  XtAddCallback(quit_btn, XtNcallback, on_quit, NULL);

  XtRealizeWidget(toplevel);
  XtAppMainLoop(app);
  return 0;
}
