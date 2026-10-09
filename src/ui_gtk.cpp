#include <gtk/gtk.h>

#include <cstdio>
#include <format>
#include <string>

#include "core.h"

namespace {

struct Theme {
  const char *bg, *fd, *tx, *ac;
};

constexpr Theme kColors[] = {
    {"#14001c", "#2c003c", "#ffdeff", "#ff00c8"},
    {"#00081a", "#001a3c", "#d4e8ff", "#0096ff"},
    {"#000", "#181818", "#d6ffd6", "#00ff50"},
    {"#eeebde", "#fffffa", "#181818", "#181818"},
};

GtkWidget *win, *host, *pw, *fan, *src, *status_label, *logv, *swatch[4], *lock[5];
GtkCssProvider* css;
size_t since = 0;
bool locked = false;

using Fn = void (*)(GtkWidget*, gpointer);

void apply(int i) {
  const Theme& t = kColors[i];
  std::string c = std::format(R"(
window{{background-color:{0};color:{2};font-family:monospace;font-size:11pt}}
label{{color:{2}}}
button label{{color:inherit}}
#title{{color:{3};font-size:32px}}
#status{{color:{3};min-height:20px}}
button{{background-image:none;background-color:{0};color:{3};border:2px solid {3};border-radius:0;box-shadow:none;text-shadow:none;padding:9px 14px}}
button:active{{background-color:{3};color:{0}}}
button:disabled{{opacity:.4}}
button.sw{{min-width:24px;min-height:24px;padding:0;border:3px solid {0}}}
button.sw.on{{border-color:{2}}}
entry{{background-image:none;background-color:{1};color:{2};caret-color:{2};border:1px solid {3};border-radius:0;box-shadow:none;padding:7px;min-height:0}}
scrolledwindow{{border:1px solid {3}}}
textview text{{background-color:{1};color:{2}}}
check{{background-image:none;background-color:{1};color:{3};border:1px solid {3};border-radius:0;box-shadow:none}}
)",
                              t.bg, t.fd, t.tx, t.ac);
  for (int k = 0; k < 4; k++) c += std::format("#sw{}{{background-image:linear-gradient(to right,{} 50%,{} 50%)}}\n", k, kColors[k].bg, kColors[k].ac);
  gtk_css_provider_load_from_data(css, c.c_str(), -1, nullptr);
  for (int k = 0; k < 4; k++) {
    GtkStyleContext* ctx = gtk_widget_get_style_context(swatch[k]);
    if (k == i) gtk_style_context_add_class(ctx, "on");
    else gtk_style_context_remove_class(ctx, "on");
  }
}

const char* text(GtkWidget* w) { return gtk_entry_get_text(GTK_ENTRY(w)); }

bool ask(const char* q) {
  GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", q);
  gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_NO);
  bool yes = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
  gtk_widget_destroy(d);
  return yes;
}

void run(const char* action, const char* source = "") {
  start_job(action, text(host), text(pw), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fan)), source);
}

void add(GtkWidget* box, GtkWidget* w, bool grow = false) { gtk_box_pack_start(GTK_BOX(box), w, grow, grow, 0); }

GtkWidget* label(const char* t) {
  GtkWidget* l = gtk_label_new(t);
  gtk_label_set_xalign(GTK_LABEL(l), 0);
  gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
  return l;
}

GtkWidget* button(const char* t, Fn f, gpointer d = nullptr) {
  GtkWidget* b = gtk_button_new_with_label(t);
  g_signal_connect(b, "clicked", G_CALLBACK(f), d);
  return b;
}

GtkWidget* hbox(int gap, bool even = false) {
  GtkWidget* b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, gap);
  gtk_box_set_homogeneous(GTK_BOX(b), even);
  return b;
}

GtkWidget* field(const char* t, GtkWidget* e) {
  GtkWidget* b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  add(b, label(t));
  add(b, e);
  return b;
}

void pick(GtkWidget*, gpointer) {
  GtkFileChooserNative* d = gtk_file_chooser_native_new("Choose a plugin", GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");
  GtkFileFilter* f = gtk_file_filter_new();
  gtk_file_filter_set_name(f, "Plugin packages (.zip, .gz, .tgz)");
  gtk_file_filter_add_pattern(f, "*.zip");
  gtk_file_filter_add_pattern(f, "*.gz");
  gtk_file_filter_add_pattern(f, "*.tgz");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), f);
  if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
    char* p = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
    if (p) gtk_entry_set_text(GTK_ENTRY(src), p);
    g_free(p);
  }
  g_object_unref(d);
}

gboolean tick(gpointer) {
  Snap s = snapshot(since);
  since = s.next;
  if (!s.log.empty()) {
    GtkTextBuffer* b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logv));
    char* v = g_utf8_make_valid(s.log.data(), (gssize)s.log.size());
    GtkTextIter e;
    gtk_text_buffer_get_end_iter(b, &e);
    gtk_text_buffer_insert(b, &e, v, -1);
    g_free(v);
    gtk_text_buffer_get_end_iter(b, &e);
    gtk_text_buffer_place_cursor(b, &e);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(logv), gtk_text_buffer_get_insert(b), 0, FALSE, 0, 0);
  }
  gtk_label_set_text(GTK_LABEL(status_label), s.status.c_str());
  if (s.busy != locked) {
    locked = s.busy;
    for (GtkWidget* w : lock) gtk_widget_set_sensitive(w, !locked);
    if (!locked) gtk_entry_set_text(GTK_ENTRY(pw), "");
  }
  return G_SOURCE_CONTINUE;
}

}  // namespace

int ui_run() {
  if (!gtk_init_check(nullptr, nullptr)) return fputs("Framey App needs a graphical desktop session.\n", stderr), 1;
  css = gtk_css_provider_new();
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(win), "Framey App");
  gtk_window_set_default_size(GTK_WINDOW(win), 640, 720);
  gtk_container_set_border_width(GTK_CONTAINER(win), 24);
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
  g_signal_connect(win, "delete-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer) -> gboolean { return locked || busy(); }), nullptr);

  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_add(GTK_CONTAINER(win), root);

  GtkWidget* head = hbox(8);
  GtkWidget* title = label("FRAMEY");
  gtk_widget_set_name(title, "title");
  add(head, title, true);
  for (int i = 0; i < 4; i++) {
    swatch[i] = button("", [](GtkWidget*, gpointer d) {
      int k = GPOINTER_TO_INT(d);
      apply(k);
      save_theme(kThemes[k]);
    }, GINT_TO_POINTER(i));
    gtk_widget_set_name(swatch[i], std::format("sw{}", i).c_str());
    gtk_style_context_add_class(gtk_widget_get_style_context(swatch[i]), "sw");
    gtk_widget_set_tooltip_text(swatch[i], kThemes[i]);
    gtk_widget_set_valign(swatch[i], GTK_ALIGN_CENTER);
    add(head, swatch[i]);
  }
  add(root, head);
  add(root, label("Before you start, on the headset:"));
  add(root, label("1. Steam Settings > System > Enable Developer Mode.\n2. Developer (left menu) > scroll to the bottom > Set User Password.\n3. Keep the headset awake and on the same network as this computer."));
  add(root, label("Then enter its address and password and click Install. The password is needed the first time and for Fan Control, and is never saved."));

  host = gtk_entry_new();
  std::string h = saved("host.txt");
  gtk_entry_set_text(GTK_ENTRY(host), h.empty() ? "frame" : h.c_str());
  pw = gtk_entry_new();
  gtk_entry_set_visibility(GTK_ENTRY(pw), FALSE);
  GtkWidget* creds = hbox(12, true);
  add(creds, field("Headset address", host), true);
  add(creds, field("Password (never saved)", pw), true);
  add(root, creds);

  fan = gtk_check_button_new_with_label("Also install Fan Control");
  add(root, fan);

  GtkWidget* bar = hbox(10, true);
  lock[0] = button("Install / Update", [](GtkWidget*, gpointer) {
    if (ask("Install or update Framey on the headset? If SteamVR is running it will be restarted, which ends the current VR session.")) run("install");
  });
  lock[1] = button("Check for updates", [](GtkWidget*, gpointer) { run("check"); });
  lock[2] = button("Remove", [](GtkWidget*, gpointer) {
    if (ask("Remove Framey, Fan Control, all plugins and their saved settings from the headset, and this app's key and data from this computer?")) run("remove");
  });
  for (int i = 0; i < 3; i++) add(bar, lock[i], true);
  add(root, bar);

  add(root, label("Install a plugin: .zip file or GitHub link"));
  src = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(src), "https://github.com/owner/repo");
  lock[3] = button("Browse...", pick);
  lock[4] = button("Add plugin", [](GtkWidget*, gpointer) {
    if (ask("A plugin runs code on your headset, inside Steam's interface. Only install plugins you trust. If SteamVR is running it will be restarted, which ends the current VR session. Continue?")) run("plugin", text(src));
  });
  GtkWidget* plug = hbox(12);
  add(plug, src, true);
  add(plug, lock[3]);
  add(plug, lock[4]);
  add(root, plug);

  status_label = label("");
  gtk_widget_set_name(status_label, "status");
  add(root, status_label);

  logv = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(logv), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(logv), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(logv), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(logv), 8);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(logv), 8);
  GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 210);
  gtk_container_add(GTK_CONTAINER(scroll), logv);
  add(root, scroll, true);

  std::string t = saved("theme.txt");
  int cur = 0;
  for (int i = 0; i < 4; i++)
    if (t == kThemes[i]) cur = i;
  apply(cur);

  g_timeout_add(500, tick, nullptr);
  gtk_widget_show_all(win);
  gtk_main();
  return 0;
}
