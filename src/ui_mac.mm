#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "core.h"

struct Theme {
  unsigned bg, fd, tx, ac;
};
static const Theme kT[] = {
  {0x14001c, 0x2c003c, 0xffdeff, 0xff00c8},
  {0x00081a, 0x001a3c, 0xd4e8ff, 0x0096ff},
  {0x000000, 0x181818, 0xd6ffd6, 0x00ff50},
  {0xeeebde, 0xfffffa, 0x181818, 0x181818},
};
static NSString* ns(const std::string& s) {
  NSString* r = [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
  return r ? r : @"";
}

static std::string cs(NSString* s) {
  const char* p = s.UTF8String;
  return p ? p : "";
}

static NSColor* rgb(unsigned v) {
  return [NSColor colorWithSRGBRed:((v >> 16) & 255) / 255.0 green:((v >> 8) & 255) / 255.0 blue:(v & 255) / 255.0 alpha:1];
}

static void box(NSView* v, unsigned fill, unsigned line, CGFloat w) {
  CGColorRef f = CGColorCreateSRGB(((fill >> 16) & 255) / 255.0, ((fill >> 8) & 255) / 255.0, (fill & 255) / 255.0, 1);
  CGColorRef l = CGColorCreateSRGB(((line >> 16) & 255) / 255.0, ((line >> 8) & 255) / 255.0, (line & 255) / 255.0, 1);
  v.wantsLayer = YES;
  v.layer.backgroundColor = f;
  v.layer.borderColor = l;
  v.layer.borderWidth = w;
  CGColorRelease(f);
  CGColorRelease(l);
}

static void split(NSView* v, unsigned left, unsigned right, unsigned line, CGFloat w) {
  box(v, right, line, w);
  v.layer.sublayers = nil;
  CALayer* l = [CALayer layer];
  l.frame = CGRectMake(0, 0, v.bounds.size.width / 2, v.bounds.size.height);
  CGColorRef c = CGColorCreateSRGB(((left >> 16) & 255) / 255.0, ((left >> 8) & 255) / 255.0, (left & 255) / 255.0, 1);
  l.backgroundColor = c;
  CGColorRelease(c);
  [v.layer addSublayer:l];
}

static NSFont* mono(CGFloat size, NSFontWeight weight) {
  return [NSFont monospacedSystemFontOfSize:size weight:weight];
}

static NSAttributedString* styled(NSString* s, NSColor* c, NSFont* f) {
  return [[NSAttributedString alloc] initWithString:s attributes:@{NSForegroundColorAttributeName: c, NSFontAttributeName: f}];
}

static NSTextField* label(NSView* p, NSString* s, CGFloat x, CGFloat y, CGFloat w, CGFloat h) {
  NSTextField* l = [NSTextField wrappingLabelWithString:s];
  l.font = mono(13, NSFontWeightRegular);
  l.frame = NSMakeRect(x, y, w, h);
  [p addSubview:l];
  return l;
}

static NSTextField* input(NSView* p, NSTextField* f, CGFloat x, CGFloat y, CGFloat w) {
  f.font = mono(13, NSFontWeightRegular);
  f.bezeled = NO;
  f.frame = NSMakeRect(x, y, w, 26);
  [p addSubview:f];
  return f;
}

static NSButton* button(NSView* p, id t, SEL a, NSString* s, CGFloat x, CGFloat y, CGFloat w, CGFloat h) {
  NSButton* b = [NSButton buttonWithTitle:s target:t action:a];
  b.bordered = NO;
  b.frame = NSMakeRect(x, y, w, h);
  [p addSubview:b];
  return b;
}

static BOOL ask(NSString* s, NSString* ok) {
  NSAlert* a = [NSAlert new];
  a.messageText = s;
  [a addButtonWithTitle:ok];
  [a addButtonWithTitle:@"Cancel"];
  return [a runModal] == NSAlertFirstButtonReturn;
}

@interface FView : NSView
@end

@implementation FView
- (BOOL)isFlipped {
  return YES;
}
@end

@interface App : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

@implementation App {
  NSWindow* _win;
  NSTextField *_title, *_status, *_host, *_src;
  NSSecureTextField* _pw;
  NSButton* _fan;
  NSScrollView* _scroll;
  NSTextView* _log;
  NSColor* _tx;
  NSMutableArray *_labels, *_fields, *_btns, *_sw;
  size_t _since;
  bool _busy;
}

- (void)applicationDidFinishLaunching:(NSNotification*)n {
  NSMenu* bar = [NSMenu new];
  NSMenuItem* mi = [NSMenuItem new];
  NSMenu* m = [NSMenu new];
  [m addItemWithTitle:@"Quit Framey App" action:@selector(terminate:) keyEquivalent:@"q"];
  mi.submenu = m;
  [bar addItem:mi];
  NSMenuItem* ei = [NSMenuItem new];
  NSMenu* em = [[NSMenu alloc] initWithTitle:@"Edit"];
  [em addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
  [em addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
  [em addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
  [em addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
  ei.submenu = em;
  [bar addItem:ei];
  NSApp.mainMenu = bar;

  _win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 640)
                                     styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
                                       backing:NSBackingStoreBuffered
                                         defer:NO];
  _win.title = @"Framey App";
  _win.releasedWhenClosed = NO;
  FView* v = [[FView alloc] initWithFrame:NSMakeRect(0, 0, 640, 640)];
  _win.contentView = v;
  _labels = [NSMutableArray array];
  _btns = [NSMutableArray array];
  _sw = [NSMutableArray array];

  _title = label(v, @"FRAMEY", 24, 18, 300, 42);
  _title.font = mono(32, NSFontWeightBold);
  for (int i = 0; i < 4; i++) {
    NSButton* b = button(v, self, @selector(pick:), @"", 472 + i * 38, 24, 30, 30);
    b.tag = i;
    b.toolTip = ns(kThemes[i]);
    b.accessibilityLabel = ns(kThemes[i]);
    [_sw addObject:b];
  }
  [_labels addObject:label(v, @"Before you start, on the headset:", 24, 70, 592, 20)];
  [_labels addObject:label(v, @"1. Steam Settings > System > Enable Developer Mode.\n"
                              "2. Developer (left menu) > scroll to the bottom > Set User Password.\n"
                              "3. Keep the headset awake and on the same network as this computer.", 24, 92, 592, 62)];
  [_labels addObject:label(v, @"Then enter its address and password and click Install. The password is needed the first time and for Fan Control, and is never saved.", 24, 164, 592, 54)];
  [_labels addObject:label(v, @"Headset address", 24, 226, 286, 18)];
  [_labels addObject:label(v, @"Password (never saved)", 330, 226, 286, 18)];
  _host = input(v, [NSTextField new], 24, 248, 286);
  _pw = (NSSecureTextField*)input(v, [NSSecureTextField new], 330, 248, 286);
  std::string h = saved("host.txt");
  _host.stringValue = h.empty() ? @"frame" : ns(h);
  _fan = [NSButton checkboxWithTitle:@"Also install Fan Control" target:nil action:nil];
  _fan.frame = NSMakeRect(24, 288, 592, 20);
  [v addSubview:_fan];
  NSString* names[] = {@"Install / Update", @"Check for updates", @"Remove"};
  for (int i = 0; i < 3; i++) {
    NSButton* b = button(v, self, @selector(job:), names[i], 24 + i * 201, 320, 190, 36);
    b.tag = i;
    [_btns addObject:b];
  }
  [_labels addObject:label(v, @"Install a plugin: .zip file or GitHub link", 24, 370, 592, 18)];
  _src = input(v, [NSTextField new], 24, 392, 358);
  _src.placeholderString = @"https://github.com/owner/repo";
  [_btns addObject:button(v, self, @selector(browse:), @"Browse...", 394, 391, 100, 28)];
  [_btns addObject:button(v, self, @selector(addPlugin:), @"Add plugin", 506, 391, 110, 28)];
  _fields = [NSMutableArray arrayWithObjects:_host, _pw, _src, nil];
  _status = label(v, @"", 24, 428, 592, 20);
  _scroll = [NSTextView scrollableTextView];
  _scroll.frame = NSMakeRect(24, 454, 592, 166);
  _log = _scroll.documentView;
  _log.editable = NO;
  _log.font = mono(12, NSFontWeightRegular);
  [v addSubview:_scroll];

  int t = 0;
  std::string name = saved("theme.txt");
  for (int i = 0; i < 4; i++)
    if (name == kThemes[i]) t = i;
  [self theme:t];
  [_win center];
  _win.delegate = self;
  [_win makeKeyAndOrderFront:nil];
  if (@available(macOS 14.0, *)) [NSApp activate];
  else [NSApp activateIgnoringOtherApps:YES];
  [NSTimer scheduledTimerWithTimeInterval:0.5 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)a {
  return YES;
}

- (BOOL)windowShouldClose:(NSWindow*)w {
  return !(_busy || busy());
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)a {
  return _busy || busy() ? NSTerminateCancel : NSTerminateNow;
}

- (void)theme:(int)i {
  const Theme& t = kT[i];
  NSColor *bg = rgb(t.bg), *fd = rgb(t.fd), *tx = rgb(t.tx), *ac = rgb(t.ac);
  _tx = tx;
  _win.backgroundColor = bg;
  _win.appearance = [NSAppearance appearanceNamed:i == 3 ? NSAppearanceNameAqua : NSAppearanceNameDarkAqua];
  for (NSTextField* l in _labels) l.textColor = tx;
  _title.textColor = ac;
  _status.textColor = ac;
  _fan.attributedTitle = styled(@"Also install Fan Control", tx, mono(13, NSFontWeightRegular));
  for (NSTextField* f in _fields) {
    f.backgroundColor = fd;
    f.textColor = tx;
    box(f, t.fd, t.ac, 1);
  }
  for (NSButton* b in _btns) {
    box(b, t.bg, t.ac, 2);
    b.attributedTitle = styled(b.title, ac, mono(13, NSFontWeightRegular));
  }
  for (NSButton* b in _sw) split(b, kT[b.tag].bg, kT[b.tag].ac, t.tx, b.tag == i ? 3 : 1);
  _log.backgroundColor = fd;
  _log.textColor = tx;
  box(_scroll, t.fd, t.ac, 1);
}

- (void)pick:(NSButton*)b {
  save_theme(kThemes[b.tag]);
  [self theme:(int)b.tag];
}

- (void)go:(const char*)action source:(NSString*)s {
  start_job(action, cs(_host.stringValue), cs(_pw.stringValue), _fan.state == NSControlStateValueOn, cs(s));
}

- (void)job:(NSButton*)b {
  static const char* kAction[] = {"install", "check", "remove"};
  if (b.tag != 1 && !ask(b.tag ? @"Remove Framey, Fan Control, all plugins and their saved settings from the headset, and this app's key and data from this computer?" : @"Install or update Framey on the headset? If SteamVR is running it will be restarted, which ends the current VR session.", b.tag ? @"Remove" : @"Install")) return;
  [self go:kAction[b.tag] source:@""];
}

- (void)addPlugin:(id)s {
  if (ask(@"A plugin runs code on your headset, inside Steam's interface. Only install plugins you trust. If SteamVR is running it will be restarted, which ends the current VR session. Continue?", @"Continue"))
    [self go:"plugin" source:_src.stringValue];
}

- (void)browse:(id)s {
  NSOpenPanel* p = [NSOpenPanel openPanel];
  NSMutableArray* types = [NSMutableArray array];
  for (NSString* e in @[@"zip", @"gz", @"tgz"]) {
    id u = [NSClassFromString(@"UTType") typeWithFilenameExtension:e];
    if (u) [types addObject:u];
  }
  p.allowedContentTypes = types;
  if ([p runModal] == NSModalResponseOK) _src.stringValue = p.URL.path;
}

- (void)tick:(NSTimer*)t {
  Snap s = snapshot(_since);
  if (!s.log.empty()) {
    [_log.textStorage appendAttributedString:styled(ns(s.log), _tx, mono(12, NSFontWeightRegular))];
    [_log scrollRangeToVisible:NSMakeRange(_log.string.length, 0)];
  }
  _since = s.next;
  _status.stringValue = ns(s.status);
  if (s.busy != _busy) {
    _busy = s.busy;
    for (NSButton* b in _btns) {
      b.enabled = !_busy;
      b.alphaValue = _busy ? 0.4 : 1;
    }
    if (!_busy) _pw.stringValue = @"";
  }
}
@end

static App* g_app;

int ui_run() {
  @autoreleasepool {
    NSApplication* a = [NSApplication sharedApplication];
    [a setActivationPolicy:NSApplicationActivationPolicyRegular];
    g_app = [App new];
    a.delegate = g_app;
    [a run];
  }
  return 0;
}
