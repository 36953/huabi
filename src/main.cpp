// 36953画笔 — 上课用的临时全屏标注。
// 运行后常驻托盘。按 Pause 把当前画面定格下来涂画，再按 Pause 或 Esc 结束。
// 笔迹只画在定格的画面上，不会写进 CAD。
// 编译：在项目目录双击 build.bat，生成 HuaBi.exe。

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_WINNT 0x0601
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#endif

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif
#ifndef NIIF_NOSOUND
#define NIIF_NOSOUND 0x00000010
#endif
#ifndef NIIF_USER
#define NIIF_USER 0x00000004
#endif
#ifndef NIIF_LARGE_ICON
#define NIIF_LARGE_ICON 0x00000020
#endif

namespace gp = Gdiplus;

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_TOGGLE = WM_APP + 2;
constexpr UINT WM_KEYSHOW = WM_APP + 3;
constexpr UINT HOTKEY_ID = 1;
constexpr UINT HOTKEY_PIN = 2;
constexpr UINT KEY_TIMER = 2;

constexpr int ACT_COLOR = 100;
constexpr int ACT_THIN = 120;
constexpr int ACT_MID = 121;
constexpr int ACT_THICK = 122;
constexpr int ACT_PEN = 130;
constexpr int ACT_LINE = 131;
constexpr int ACT_RECT = 132;
constexpr int ACT_MARKER = 133;
constexpr int ACT_ERASER = 134;
constexpr int ACT_UNDO = 140;
constexpr int ACT_CLEAR = 141;
constexpr int ACT_SAVE = 142;
constexpr int ACT_EXIT = 143;
constexpr int ACT_REDO = 144;

constexpr int ID_TOGGLE = 2001;
constexpr int ID_HELP = 2002;
constexpr int ID_QUIT = 2003;
constexpr int ID_PIN = 2004;
constexpr int ID_PIN_CLIP = 2005;
constexpr int ID_HK_BASE = 2100;
constexpr int PIN_CLOSE = 3101;
constexpr int PIN_CLOSE_ALL = 3102;
constexpr int PIN_COPY = 3103;
constexpr int PIN_SAVE = 3104;

constexpr float kWidthThin = 4.f;
constexpr float kWidthMid = 8.f;
constexpr float kWidthThick = 18.f;
const wchar_t kAppTitle[] = L"36953画笔 V1.0";

enum class Tool { Pen = 0, Line = 1, Rect = 2, Marker = 3, Eraser = 4 };
enum class Gesture { None, Draw, Ui, Erase };

struct Hotkey {
  UINT mods;
  UINT vk;
  const wchar_t* label;
  const wchar_t* menu;
};

const Hotkey kHotkeys[] = {
    {0, VK_PAUSE, L"Pause", L"Pause（单键，上课用这个）"},
    {MOD_CONTROL | MOD_ALT, 'D', L"Ctrl+Alt+D", L"Ctrl+Alt+D"},
    {MOD_CONTROL | MOD_SHIFT, 'D', L"Ctrl+Shift+D", L"Ctrl+Shift+D"},
    {MOD_CONTROL, VK_OEM_3, L"Ctrl+`", L"Ctrl + 左上角 ` 键"},
};
const int kHotkeyCount = (int)(sizeof(kHotkeys) / sizeof(kHotkeys[0]));

const COLORREF kColors[] = {
    RGB(235, 45, 45),
    RGB(255, 196, 0),
    RGB(0, 176, 80),
    RGB(0, 122, 255),
    RGB(255, 255, 255),
    RGB(24, 24, 24),
};
const int kColorCount = (int)(sizeof(kColors) / sizeof(kColors[0]));

struct Stroke {
  Tool tool = Tool::Pen;
  COLORREF color = RGB(235, 45, 45);
  float width = 8.f;
  std::vector<POINT> points;
};

struct Item {
  RECT rc{};
  int action = 0;
  const wchar_t* text = nullptr;
  COLORREF color = 0;
};

struct KeyChip {
  std::wstring text;
};

struct Pin {
  HWND hwnd = nullptr;
  HBITMAP bmp = nullptr;
  int imgW = 0;
  int imgH = 0;
  float scale = 1.f;
  bool dragging = false;
  int dragX = 0;
  int dragY = 0;
};

struct Surface {
  HDC dc = nullptr;
  HBITMAP bmp = nullptr;
  HGDIOBJ old = nullptr;
  int w = 0;
  int h = 0;

  bool ok() const { return dc && bmp; }

  void Free() {
    if (dc) {
      if (old) SelectObject(dc, old);
      if (bmp) DeleteObject(bmp);
      DeleteDC(dc);
    }
    dc = nullptr;
    bmp = nullptr;
    old = nullptr;
    w = 0;
    h = 0;
  }
};

struct App {
  HINSTANCE hinst = nullptr;
  HWND host = nullptr;
  HWND overlay = nullptr;
  HWND prevFore = nullptr;
  HICON icon = nullptr;
  HANDLE mutex = nullptr;
  NOTIFYICONDATAW nid{};
  bool trayAdded = false;
  bool quitting = false;
  bool testMode = false;
  bool needCo = false;
  ULONG_PTR gdip = 0;

  std::wstring iniPath;
  int hotkeyIndex = 0;
  wchar_t hintBuf[160]{};
  wchar_t tipBuf[160]{};
  wchar_t toast[160]{};

  int vx = 0;
  int vy = 0;
  int w = 0;
  int h = 0;
  float dpiScale = 1.f;
  Surface bg, canvas, back;

  Tool tool = Tool::Pen;
  int colorIndex = 0;
  float logicalWidth = kWidthThin;
  std::vector<Stroke> strokes;
  std::vector<std::vector<Stroke>> undo;
  std::vector<std::vector<Stroke>> redo;
  Stroke live;
  bool liveActive = false;
  bool straight = false;
  bool eraseUndoPushed = false;
  Gesture gesture = Gesture::None;
  int pressed = 0;
  int hover = 0;
  POINT mouse{0, 0};
  DWORD lastToggle = 0;

  std::vector<Item> items;
  RECT barInner{};
  RECT barRect{};
  RECT hintRect{};
  RECT toastRect{};

  HWND selectWnd = nullptr;
  HWND keyWnd = nullptr;
  HHOOK keyHook = nullptr;
  Surface shot;
  bool shotDragging = false;
  POINT shotA{};
  POINT shotB{};
  int shotX = 0;
  int shotY = 0;
  std::vector<std::wstring> keyQueue;
  std::vector<KeyChip> keyChips;
  DWORD keyShownAt = 0;
  BYTE keyAlpha = 0;
  std::vector<Pin*> pins;
};

App g;

void SaveConfig();
void RefreshTexts();
void UpdateTip();
void PresentRegion(RECT dirty, HDC dest);
void PresentFull();
void RebuildCanvas();
void ShowToast(const wchar_t* text);
void LayoutUi();
void EnterDraw();
void ExitDraw();
void ToggleDraw();
void DoAction(int act);
void Quit();
void PushUndo();
void ClearAll();
void Undo();
void Redo();
void SavePng();
void CommitLive();
void HideKeys();
void EndSelect();
void CloseAllPins();
void StartPin();
void PinClipboard();
std::wstring NewPicturePath();
void NotifySaved(bool ok);

template <size_t N>
void CopyText(wchar_t (&dst)[N], const wchar_t* src) {
  if (!src) src = L"";
  wcsncpy(dst, src, N - 1);
  dst[N - 1] = L'\0';
}

template <size_t N, typename... Args>
void FormatText(wchar_t (&dst)[N], const wchar_t* fmt, Args... args) {
#if defined(_MSC_VER)
  swprintf_s(dst, fmt, args...);
#else
  swprintf(dst, N, fmt, args...);
#endif
}

int Px(int v) { return (int)std::lround(v * g.dpiScale); }

gp::Color ToColor(COLORREF c, BYTE a) {
  return gp::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

float DistSeg(float px, float py, float ax, float ay, float bx, float by) {
  float dx = bx - ax;
  float dy = by - ay;
  float len2 = dx * dx + dy * dy;
  float t = 0.f;
  if (len2 > 0.001f) {
    t = ((px - ax) * dx + (py - ay) * dy) / len2;
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
  }
  float qx = ax + t * dx;
  float qy = ay + t * dy;
  float ex = px - qx;
  float ey = py - qy;
  return std::sqrt(ex * ex + ey * ey);
}

RECT UnionR(RECT a, RECT b) {
  RECT out{};
  UnionRect(&out, &a, &b);
  return out;
}

bool Intersects(const RECT& a, const RECT& b) {
  RECT out;
  return IntersectRect(&out, &a, &b) != FALSE;
}

float ActiveWidth(Tool t) {
  float s = g.dpiScale;
  float w = g.logicalWidth;
  if (t == Tool::Marker) return std::clamp((w * 2.6f + 8.f) * s, 8.f, 90.f * s);
  if (t == Tool::Eraser) return std::clamp((w * 1.8f + 10.f) * s, 8.f, 110.f * s);
  return std::clamp(w * s, 1.f, 48.f * s);
}

RECT StrokeBounds(const Stroke& s) {
  if (s.points.empty()) return RECT{0, 0, 0, 0};
  int minx = s.points[0].x;
  int maxx = minx;
  int miny = s.points[0].y;
  int maxy = miny;
  for (const POINT& p : s.points) {
    minx = (std::min)(minx, (int)p.x);
    maxx = (std::max)(maxx, (int)p.x);
    miny = (std::min)(miny, (int)p.y);
    maxy = (std::max)(maxy, (int)p.y);
  }
  int pad = (int)std::ceil(s.width * 0.5f) + 8;
  return RECT{minx - pad, miny - pad, maxx + pad, maxy + pad};
}

bool HitStroke(const Stroke& s, float x, float y, float rad) {
  if (s.points.empty()) return false;
  float thresh = rad + s.width * 0.5f;
  if (s.tool == Tool::Rect && s.points.size() >= 2) {
    float l = (float)(std::min)(s.points[0].x, s.points[1].x);
    float r = (float)(std::max)(s.points[0].x, s.points[1].x);
    float t = (float)(std::min)(s.points[0].y, s.points[1].y);
    float b = (float)(std::max)(s.points[0].y, s.points[1].y);
    return DistSeg(x, y, l, t, r, t) <= thresh || DistSeg(x, y, r, t, r, b) <= thresh ||
           DistSeg(x, y, r, b, l, b) <= thresh || DistSeg(x, y, l, b, l, t) <= thresh;
  }
  if (s.points.size() == 1) {
    float dx = x - s.points[0].x;
    float dy = y - s.points[0].y;
    return dx * dx + dy * dy <= thresh * thresh;
  }
  for (size_t i = 1; i < s.points.size(); ++i) {
    if (DistSeg(x, y, (float)s.points[i - 1].x, (float)s.points[i - 1].y, (float)s.points[i].x,
                (float)s.points[i].y) <= thresh)
      return true;
  }
  return false;
}

RECT EraserRect() {
  float r = ActiveWidth(Tool::Eraser);
  return RECT{(int)std::floor(g.mouse.x - r - 3), (int)std::floor(g.mouse.y - r - 3),
              (int)std::ceil(g.mouse.x + r + 3), (int)std::ceil(g.mouse.y + r + 3)};
}

bool BlocksDrawing(int x, int y) {
  POINT p{x, y};
  return PtInRect(&g.barInner, p) || PtInRect(&g.hintRect, p);
}

int HitTest(int x, int y) {
  POINT p{x, y};
  for (const Item& it : g.items) {
    if (PtInRect(&it.rc, p)) return it.action;
  }
  return 0;
}

bool NearWidth(float target) { return std::fabs(g.logicalWidth - target) < 0.35f; }

bool ItemSelected(const Item& it) {
  if (it.action >= ACT_COLOR && it.action < ACT_COLOR + kColorCount)
    return g.colorIndex == it.action - ACT_COLOR;
  if (it.action == ACT_THIN) return NearWidth(kWidthThin);
  if (it.action == ACT_MID) return NearWidth(kWidthMid);
  if (it.action == ACT_THICK) return NearWidth(kWidthThick);
  if (it.action == ACT_PEN) return g.tool == Tool::Pen;
  if (it.action == ACT_LINE) return g.tool == Tool::Line;
  if (it.action == ACT_RECT) return g.tool == Tool::Rect;
  if (it.action == ACT_MARKER) return g.tool == Tool::Marker;
  if (it.action == ACT_ERASER) return g.tool == Tool::Eraser;
  return false;
}

void DrawStroke(gp::Graphics& gfx, const Stroke& s) {
  if (s.points.empty() || s.tool == Tool::Eraser) return;
  BYTE alpha = s.tool == Tool::Marker ? 108 : 255;
  gp::Color color = ToColor(s.color, alpha);
  gp::Pen pen(color, (std::max)(s.width, 1.f));
  pen.SetStartCap(gp::LineCapRound);
  pen.SetEndCap(gp::LineCapRound);
  pen.SetLineJoin(gp::LineJoinRound);

  if (s.tool == Tool::Rect && s.points.size() >= 2) {
    float x = (float)(std::min)(s.points[0].x, s.points[1].x);
    float y = (float)(std::min)(s.points[0].y, s.points[1].y);
    float rw = (float)std::abs(s.points[1].x - s.points[0].x);
    float rh = (float)std::abs(s.points[1].y - s.points[0].y);
    if (rw < 1.f && rh < 1.f) {
      gp::SolidBrush brush(color);
      float d = (std::max)(s.width, 1.f);
      gfx.FillEllipse(&brush, x - d / 2.f, y - d / 2.f, d, d);
    } else if (rw < 1.f || rh < 1.f) {
      gfx.DrawLine(&pen, x, y, x + rw, y + rh);
    } else {
      gfx.DrawRectangle(&pen, x, y, rw, rh);
    }
    return;
  }

  if (s.points.size() == 1) {
    gp::SolidBrush brush(color);
    float d = (std::max)(s.width, 1.f);
    gfx.FillEllipse(&brush, s.points[0].x - d / 2.f, s.points[0].y - d / 2.f, d, d);
    return;
  }

  std::vector<gp::PointF> pts;
  pts.reserve(s.points.size());
  for (const POINT& p : s.points) pts.push_back(gp::PointF((gp::REAL)p.x, (gp::REAL)p.y));
  gfx.DrawLines(&pen, pts.data(), (INT)pts.size());
}

void DrawEraserRing(gp::Graphics& gfx) {
  float r = ActiveWidth(Tool::Eraser);
  gp::Pen dark(gp::Color(170, 0, 0, 0), 2.f);
  gp::Pen light(gp::Color(230, 255, 255, 255), 1.2f);
  gfx.DrawEllipse(&dark, g.mouse.x - r - 1.f, g.mouse.y - r - 1.f, r * 2.f + 2.f, r * 2.f + 2.f);
  gfx.DrawEllipse(&light, g.mouse.x - r, g.mouse.y - r, r * 2.f, r * 2.f);
}

void AddRoundRect(gp::GraphicsPath& path, RECT rc, float radius) {
  gp::RectF r((gp::REAL)rc.left, (gp::REAL)rc.top, (gp::REAL)(rc.right - rc.left),
              (gp::REAL)(rc.bottom - rc.top));
  if (r.Width < 2.f || r.Height < 2.f) return;
  float d = radius * 2.f;
  if (d > r.Width) d = r.Width;
  if (d > r.Height) d = r.Height;
  path.AddArc(r.X, r.Y, d, d, 180, 90);
  path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
  path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
  path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
  path.CloseFigure();
}

void FillRound(gp::Graphics& gfx, RECT rc, float radius, gp::Color color) {
  gp::GraphicsPath path;
  AddRoundRect(path, rc, radius);
  gp::SolidBrush brush(color);
  gfx.FillPath(&brush, &path);
}

const gp::Font* PickFont(gp::Font& yahei, gp::Font& segoe) {
  return yahei.IsAvailable() ? &yahei : &segoe;
}

void DrawChrome(gp::Graphics& gfx) {
  gp::Font yahei(L"Microsoft YaHei", (gp::REAL)Px(15), gp::FontStyleRegular, gp::UnitPixel);
  gp::Font segoe(L"Segoe UI", (gp::REAL)Px(15), gp::FontStyleRegular, gp::UnitPixel);
  const gp::Font* font = PickFont(yahei, segoe);
  gp::StringFormat fmt;
  fmt.SetAlignment(gp::StringAlignmentCenter);
  fmt.SetLineAlignment(gp::StringAlignmentCenter);
  fmt.SetFormatFlags(gp::StringFormatFlagsNoWrap);
  fmt.SetTrimming(gp::StringTrimmingEllipsisCharacter);

  if (g.hintRect.right > g.hintRect.left) {
    FillRound(gfx, g.hintRect, (float)Px(16), gp::Color(255, 32, 34, 38));
    gp::SolidBrush text(gp::Color(255, 245, 245, 245));
    gp::RectF box((gp::REAL)g.hintRect.left, (gp::REAL)g.hintRect.top,
                  (gp::REAL)(g.hintRect.right - g.hintRect.left),
                  (gp::REAL)(g.hintRect.bottom - g.hintRect.top));
    gfx.DrawString(g.hintBuf, -1, font, box, &fmt, &text);
  }

  if (g.toast[0] && g.toastRect.right > g.toastRect.left) {
    FillRound(gfx, g.toastRect, (float)Px(14), gp::Color(255, 18, 92, 58));
    gp::SolidBrush text(gp::Color(255, 245, 255, 245));
    gp::RectF box((gp::REAL)g.toastRect.left, (gp::REAL)g.toastRect.top,
                  (gp::REAL)(g.toastRect.right - g.toastRect.left),
                  (gp::REAL)(g.toastRect.bottom - g.toastRect.top));
    gfx.DrawString(g.toast, -1, font, box, &fmt, &text);
  }

  if (g.barInner.right <= g.barInner.left) return;
  RECT shadow = g.barInner;
  OffsetRect(&shadow, 0, Px(3));
  FillRound(gfx, shadow, (float)Px(16), gp::Color(50, 0, 0, 0));
  FillRound(gfx, g.barInner, (float)Px(16), gp::Color(255, 32, 34, 38));

  for (const Item& it : g.items) {
    bool selected = ItemSelected(it);
    bool hover = g.hover == it.action;
    bool pressed = g.gesture == Gesture::Ui && g.pressed == it.action;
    bool enabled = !(it.action == ACT_UNDO && g.undo.empty() && !g.liveActive);
    if (it.text == nullptr) {
      float inset = (float)Px(selected ? 6 : 8);
      float x = (float)it.rc.left + inset;
      float y = (float)it.rc.top + inset;
      float side = (float)(it.rc.bottom - it.rc.top) - inset * 2.f;
      if (hover || pressed) {
        gp::SolidBrush halo(gp::Color(50, 255, 255, 255));
        gfx.FillEllipse(&halo, (float)it.rc.left + Px(4), (float)it.rc.top + Px(4),
                        (float)(it.rc.right - it.rc.left) - Px(8),
                        (float)(it.rc.bottom - it.rc.top) - Px(8));
      }
      gp::SolidBrush dot(ToColor(it.color, 255));
      gfx.FillEllipse(&dot, x, y, side, side);
      gp::Pen ring(gp::Color(230, 255, 255, 255), selected ? (float)Px(2) : 1.f);
      gfx.DrawEllipse(&ring, x, y, side, side);
      continue;
    }
    if (selected || hover || pressed) {
      RECT chip = it.rc;
      InflateRect(&chip, -Px(2), -Px(3));
      unsigned alpha = pressed ? 100 : selected ? 76 : 42;
      FillRound(gfx, chip, (float)Px(8), gp::Color(alpha, 255, 255, 255));
    }
    gp::Color tc(255, 245, 245, 245);
    if (!enabled) tc = gp::Color(255, 125, 128, 134);
    else if (it.action == ACT_EXIT) tc = gp::Color(255, 255, 176, 166);
    gp::SolidBrush text(tc);
    gp::RectF box((gp::REAL)it.rc.left, (gp::REAL)it.rc.top, (gp::REAL)(it.rc.right - it.rc.left),
                  (gp::REAL)(it.rc.bottom - it.rc.top));
    gfx.DrawString(it.text, -1, font, box, &fmt, &text);
  }
}

void PresentRegion(RECT dirty, HDC dest) {
  if (!g.overlay || !g.back.dc || !g.canvas.dc) return;
  if (dirty.right <= dirty.left || dirty.bottom <= dirty.top) return;
  InflateRect(&dirty, 3, 3);
  RECT screen{0, 0, g.w, g.h};
  RECT clip;
  if (!IntersectRect(&clip, &dirty, &screen)) return;
  int rw = clip.right - clip.left;
  int rh = clip.bottom - clip.top;
  BitBlt(g.back.dc, clip.left, clip.top, rw, rh, g.canvas.dc, clip.left, clip.top, SRCCOPY);
  {
    gp::Graphics gfx(g.back.dc);
    gfx.SetClip(gp::Rect(clip.left, clip.top, rw, rh));
    gfx.SetSmoothingMode(gp::SmoothingModeAntiAlias);
    gfx.SetCompositingMode(gp::CompositingModeSourceOver);
    gfx.SetPixelOffsetMode(gp::PixelOffsetModeHalf);
    gfx.SetTextRenderingHint(gp::TextRenderingHintAntiAliasGridFit);
    if (g.liveActive && Intersects(clip, StrokeBounds(g.live))) DrawStroke(gfx, g.live);
    if (g.tool == Tool::Eraser && Intersects(clip, EraserRect())) DrawEraserRing(gfx);
    if (Intersects(clip, g.barRect) || Intersects(clip, g.hintRect) ||
        (g.toast[0] && Intersects(clip, g.toastRect)))
      DrawChrome(gfx);
  }
  HDC target = dest;
  HDC owned = nullptr;
  if (!target) {
    owned = GetDC(g.overlay);
    target = owned;
  }
  if (target) BitBlt(target, clip.left, clip.top, rw, rh, g.back.dc, clip.left, clip.top, SRCCOPY);
  if (owned) ReleaseDC(g.overlay, owned);
}

void PresentFull() { PresentRegion(RECT{0, 0, g.w, g.h}, nullptr); }

void ShowToast(const wchar_t* text) {
  CopyText(g.toast, text ? text : L"");
  if (!g.overlay) return;
  SetTimer(g.overlay, 1, 1700, nullptr);
  PresentRegion(g.toastRect, nullptr);
}

void RebuildCanvas() {
  if (!g.canvas.dc || !g.bg.dc) return;
  BitBlt(g.canvas.dc, 0, 0, g.w, g.h, g.bg.dc, 0, 0, SRCCOPY);
  gp::Graphics gfx(g.canvas.dc);
  gfx.SetSmoothingMode(gp::SmoothingModeAntiAlias);
  gfx.SetCompositingMode(gp::CompositingModeSourceOver);
  gfx.SetPixelOffsetMode(gp::PixelOffsetModeHalf);
  for (const Stroke& s : g.strokes) DrawStroke(gfx, s);
}

void PushUndo() {
  g.undo.push_back(g.strokes);
  if (g.undo.size() > 40) g.undo.erase(g.undo.begin());
  g.redo.clear();
}

void Undo() {
  if (g.liveActive) {
    RECT bounds = StrokeBounds(g.live);
    g.liveActive = false;
    g.live.points.clear();
    g.gesture = Gesture::None;
    PresentRegion(bounds, nullptr);
    PresentRegion(g.barRect, nullptr);
    return;
  }
  if (g.undo.empty()) return;
  g.redo.push_back(std::move(g.strokes));
  g.strokes = std::move(g.undo.back());
  g.undo.pop_back();
  RebuildCanvas();
  PresentFull();
}

void Redo() {
  if (g.liveActive || g.redo.empty()) return;
  g.undo.push_back(std::move(g.strokes));
  g.strokes = std::move(g.redo.back());
  g.redo.pop_back();
  RebuildCanvas();
  PresentFull();
}

void ClearAll() {
  RECT liveBounds{0, 0, 0, 0};
  if (g.liveActive) {
    liveBounds = StrokeBounds(g.live);
    g.liveActive = false;
    g.live.points.clear();
    g.gesture = Gesture::None;
  }
  if (!g.strokes.empty()) {
    PushUndo();
    g.strokes.clear();
    RebuildCanvas();
  }
  if (g.overlay) PresentFull();
  else PresentRegion(liveBounds, nullptr);
}

void CommitLive() {
  if (!g.liveActive || g.live.points.empty()) {
    g.liveActive = false;
    return;
  }
  PushUndo();
  g.strokes.push_back(g.live);
  {
    gp::Graphics gfx(g.canvas.dc);
    gfx.SetSmoothingMode(gp::SmoothingModeAntiAlias);
    gfx.SetCompositingMode(gp::CompositingModeSourceOver);
    gfx.SetPixelOffsetMode(gp::PixelOffsetModeHalf);
    DrawStroke(gfx, g.strokes.back());
  }
  RECT bounds = StrokeBounds(g.strokes.back());
  g.liveActive = false;
  g.live.points.clear();
  PresentRegion(bounds, nullptr);
  PresentRegion(g.barRect, nullptr);
}

void EraseAt(int x, int y, RECT also) {
  RECT dirty = UnionR(also, EraserRect());
  bool removed = false;
  for (auto it = g.strokes.begin(); it != g.strokes.end();) {
    if (HitStroke(*it, (float)x, (float)y, ActiveWidth(Tool::Eraser))) {
      if (!g.eraseUndoPushed) {
        PushUndo();
        g.eraseUndoPushed = true;
      }
      dirty = UnionR(dirty, StrokeBounds(*it));
      it = g.strokes.erase(it);
      removed = true;
    } else {
      ++it;
    }
  }
  if (removed) RebuildCanvas();
  PresentRegion(dirty, nullptr);
  if (removed) PresentRegion(g.barRect, nullptr);
}

bool FindEncoder(const wchar_t* mime, CLSID* out) {
  UINT count = 0;
  UINT bytes = 0;
  if (gp::GetImageEncodersSize(&count, &bytes) != gp::Ok || bytes == 0) return false;
  std::vector<BYTE> buf(bytes);
  auto* info = reinterpret_cast<gp::ImageCodecInfo*>(buf.data());
  if (gp::GetImageEncoders(count, bytes, info) != gp::Ok) return false;
  for (UINT i = 0; i < count; ++i) {
    if (info[i].MimeType && wcscmp(info[i].MimeType, mime) == 0) {
      *out = info[i].Clsid;
      return true;
    }
  }
  return false;
}

std::wstring PicturesPenDir() {
  wchar_t pic[MAX_PATH];
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, SHGFP_TYPE_CURRENT, pic))) return L"";
  std::wstring dir = std::wstring(pic) + L"\\画笔";
  if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return L"";
  return dir;
}

std::wstring NewPicturePath() {
  std::wstring dir = PicturesPenDir();
  if (dir.empty()) return L"";
  SYSTEMTIME now;
  GetLocalTime(&now);
  wchar_t stamp[48];
  FormatText(stamp, L"%04d%02d%02d-%02d%02d%02d-%03d", (int)now.wYear, (int)now.wMonth, (int)now.wDay,
             (int)now.wHour, (int)now.wMinute, (int)now.wSecond, (int)(GetTickCount() % 1000));
  return dir + L"\\画笔-" + stamp + L".png";
}

bool SaveHbitmapPng(HBITMAP bmp, const std::wstring& path) {
  if (!bmp || path.empty()) return false;
  CLSID clsid{};
  if (!FindEncoder(L"image/png", &clsid)) return false;
  gp::Bitmap* image = gp::Bitmap::FromHBITMAP(bmp, nullptr);
  if (!image || image->GetLastStatus() != gp::Ok) {
    delete image;
    return false;
  }
  gp::Status saved = image->Save(path.c_str(), &clsid, nullptr);
  delete image;
  return saved == gp::Ok;
}

void NotifySaved(bool ok) {
  const wchar_t* text = ok ? L"已保存到图片\\画笔" : L"保存失败";
  if (g.overlay) {
    ShowToast(text);
    return;
  }
  if (!g.trayAdded) return;
  g.nid.uFlags = NIF_INFO;
  CopyText(g.nid.szInfoTitle, kAppTitle);
  CopyText(g.nid.szInfo, ok ? L"已保存到“图片\\画笔”文件夹。" : L"保存失败。");
  g.nid.hBalloonIcon = g.icon;
  g.nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON | NIIF_NOSOUND;
  g.nid.uTimeout = 4000;
  Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

void SavePng() {
  if (!g.canvas.dc) return;
  if (g.liveActive) CommitLive();
  std::wstring path = NewPicturePath();
  if (path.empty()) {
    ShowToast(L"找不到图片文件夹");
    return;
  }
  SelectObject(g.canvas.dc, g.canvas.old);
  bool saved = SaveHbitmapPng(g.canvas.bmp, path);
  SelectObject(g.canvas.dc, g.canvas.bmp);
  ShowToast(saved ? L"已保存到图片\\画笔" : L"保存失败");
}

void AppendPoint(int x, int y) {
  std::vector<POINT>& pts = g.live.points;
  if (pts.empty()) {
    pts.push_back(POINT{x, y});
    return;
  }
  bool straight = g.live.tool == Tool::Line || g.live.tool == Tool::Rect ||
                  (g.live.tool == Tool::Pen && g.straight);
  if (straight) {
    POINT start = pts.front();
    pts.clear();
    pts.push_back(start);
    pts.push_back(POINT{x, y});
    return;
  }
  POINT last = pts.back();
  int dx = x - last.x;
  int dy = y - last.y;
  if (dx * dx + dy * dy >= 3) pts.push_back(POINT{x, y});
}

bool IsOurWindow(HWND hwnd) {
  if (!hwnd) return false;
  if (hwnd == g.host || hwnd == g.overlay || hwnd == g.selectWnd || hwnd == g.keyWnd) return true;
  wchar_t cls[64];
  return GetClassNameW(hwnd, cls, 64) > 0 && wcscmp(cls, L"HuaBiPin") == 0;
}

void RememberForeground() {
  HWND fore = GetForegroundWindow();
  if (!fore || IsOurWindow(fore)) return;
  g.prevFore = fore;
}

void FocusOverlay() {
  if (!g.overlay) return;
  HWND fore = GetForegroundWindow();
  DWORD foreThread = fore ? GetWindowThreadProcessId(fore, nullptr) : 0;
  DWORD mine = GetCurrentThreadId();
  BOOL attached = FALSE;
  if (foreThread && foreThread != mine) attached = AttachThreadInput(foreThread, mine, TRUE);
  SetForegroundWindow(g.overlay);
  SetFocus(g.overlay);
  if (attached) AttachThreadInput(foreThread, mine, FALSE);
}

void SyncMouse() {
  if (!g.overlay) return;
  POINT pt;
  GetCursorPos(&pt);
  ScreenToClient(g.overlay, &pt);
  g.mouse = pt;
}

RECT AnchorRect() {
  RECT window{0, 0, g.w, g.h};
  if (g.testMode) return window;
  POINT pt;
  GetCursorPos(&pt);
  HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{sizeof(info)};
  if (!GetMonitorInfoW(mon, &info)) return window;
  RECT rect = info.rcMonitor;
  OffsetRect(&rect, -g.vx, -g.vy);
  RECT out;
  if (!IntersectRect(&out, &rect, &window)) return window;
  return out;
}

void UpdateDpi(RECT screenRect) {
  g.dpiScale = 1.f;
  POINT pt{screenRect.left + 10, screenRect.top + 10};
  HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
  HMODULE shcore = LoadLibraryW(L"Shcore.dll");
  if (!shcore) return;
  using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
  auto fn = reinterpret_cast<Fn>(GetProcAddress(shcore, "GetDpiForMonitor"));
  if (fn) {
    UINT x = 96;
    UINT y = 96;
    if (SUCCEEDED(fn(mon, 0, &x, &y)) && x >= 96) g.dpiScale = x / 96.f;
  }
  FreeLibrary(shcore);
}

void LayoutUi() {
  g.items.clear();
  RECT anchor = AnchorRect();

  struct Spec {
    int action;
    const wchar_t* text;
    int w;
    COLORREF color;
    int row;
    bool section;
  };
  std::vector<Spec> specs;
  for (int i = 0; i < kColorCount; ++i)
    specs.push_back(Spec{ACT_COLOR + i, nullptr, Px(34), kColors[i], 0, false});
  specs.push_back(Spec{ACT_THIN, L"细", Px(42), 0, 0, true});
  specs.push_back(Spec{ACT_MID, L"中", Px(42), 0, 0, false});
  specs.push_back(Spec{ACT_THICK, L"粗", Px(42), 0, 0, false});
  specs.push_back(Spec{ACT_PEN, L"画笔", Px(58), 0, 1, false});
  specs.push_back(Spec{ACT_LINE, L"直线", Px(58), 0, 1, false});
  specs.push_back(Spec{ACT_RECT, L"矩形", Px(58), 0, 1, false});
  specs.push_back(Spec{ACT_MARKER, L"荧光", Px(58), 0, 1, false});
  specs.push_back(Spec{ACT_ERASER, L"橡皮", Px(58), 0, 1, false});
  specs.push_back(Spec{ACT_UNDO, L"撤销", Px(58), 0, 1, true});
  specs.push_back(Spec{ACT_CLEAR, L"清空", Px(58), 0, 1, false});
  specs.push_back(Spec{ACT_SAVE, L"保存", Px(58), 0, 1, false});
  specs.push_back(Spec{ACT_EXIT, L"结束", Px(58), 0, 1, false});

  const int gap = Px(6);
  const int section = Px(14);
  const int rowH = Px(40);
  const int padX = Px(12);
  const int padY = Px(10);
  const int rowGap = Px(6);

  int rowW[2] = {0, 0};
  bool first[2] = {true, true};
  for (const Spec& s : specs) {
    if (!first[s.row]) rowW[s.row] += s.section ? section : gap;
    first[s.row] = false;
    rowW[s.row] += s.w;
  }
  int anchorW = anchor.right - anchor.left;
  int oneRow = rowW[0] + section + rowW[1];
  bool single = oneRow + padX * 2 <= anchorW - Px(16);
  int rows = 2;
  int contentW = (std::max)(rowW[0], rowW[1]);
  if (single) {
    for (Spec& s : specs) {
      if (s.action == ACT_PEN) s.section = true;
      s.row = 0;
    }
    rows = 1;
    contentW = oneRow;
  }
  int barW = contentW + padX * 2;
  int barH = padY * 2 + rows * rowH + (rows - 1) * rowGap;
  if (barW > anchorW - Px(8)) barW = anchorW - Px(8);
  int cx = (anchor.left + anchor.right) / 2;
  int left = cx - barW / 2;
  if (left < anchor.left + 4) left = anchor.left + 4;
  if (left + barW > anchor.right - 4) left = anchor.right - 4 - barW;
  int bottom = anchor.bottom - Px(16);
  g.barInner = RECT{left, bottom - barH, left + barW, bottom};
  g.barRect = g.barInner;
  InflateRect(&g.barRect, Px(8), Px(12));
  RECT window{0, 0, g.w, g.h};
  RECT cut;
  if (IntersectRect(&cut, &g.barRect, &window)) g.barRect = cut;

  for (int row = 0; row < rows; ++row) {
    int width = 0;
    bool rowFirst = true;
    for (const Spec& s : specs) {
      if (s.row != row) continue;
      if (!rowFirst) width += s.section ? section : gap;
      rowFirst = false;
      width += s.w;
    }
    int x = g.barInner.left + (barW - width) / 2;
    int y = g.barInner.top + padY + row * (rowH + rowGap);
    rowFirst = true;
    for (const Spec& s : specs) {
      if (s.row != row) continue;
      if (!rowFirst) x += s.section ? section : gap;
      rowFirst = false;
      RECT rc{x, y, x + s.w, y + rowH};
      g.items.push_back(Item{rc, s.action, s.text, s.color});
      x += s.w;
    }
  }

  gp::Font yahei(L"Microsoft YaHei", (gp::REAL)Px(16), gp::FontStyleRegular, gp::UnitPixel);
  gp::Font segoe(L"Segoe UI", (gp::REAL)Px(16), gp::FontStyleRegular, gp::UnitPixel);
  const gp::Font* font = PickFont(yahei, segoe);
  gp::RectF measured;
  {
    gp::Graphics gfx(g.back.dc);
    gfx.MeasureString(g.hintBuf, -1, font, gp::PointF(0, 0), &measured);
  }
  int hintW = (int)std::ceil(measured.Width) + Px(36);
  int hintH = Px(36);
  int maxW = anchorW - Px(20);
  if (hintW < Px(220)) hintW = Px(220);
  if (hintW > maxW) hintW = maxW;
  int hintLeft = cx - hintW / 2;
  if (hintLeft < anchor.left + 8) hintLeft = anchor.left + 8;
  int hintTop = anchor.top + Px(14);
  g.hintRect = RECT{hintLeft, hintTop, hintLeft + hintW, hintTop + hintH};

  int toastW = (std::min)(Px(420), maxW);
  int toastLeft = cx - toastW / 2;
  if (toastLeft < anchor.left + 8) toastLeft = anchor.left + 8;
  int toastTop = g.hintRect.bottom + Px(8);
  g.toastRect = RECT{toastLeft, toastTop, toastLeft + toastW, toastTop + Px(34)};
}

void RefreshWidthVisuals() {
  if (g.liveActive) {
    RECT before = StrokeBounds(g.live);
    g.live.width = ActiveWidth(g.live.tool);
    PresentRegion(UnionR(before, StrokeBounds(g.live)), nullptr);
  }
  if (g.tool == Tool::Eraser) PresentRegion(EraserRect(), nullptr);
  PresentRegion(g.barRect, nullptr);
  wchar_t text[32];
  FormatText(text, L"粗细 %d", (int)std::lround(g.logicalWidth));
  ShowToast(text);
}

void SetTool(Tool tool) {
  g.tool = tool;
  SaveConfig();
  PresentRegion(EraserRect(), nullptr);
  PresentRegion(g.barRect, nullptr);
}

void DoAction(int act) {
  if (act >= ACT_COLOR && act < ACT_COLOR + kColorCount) {
    g.colorIndex = act - ACT_COLOR;
    SaveConfig();
    PresentRegion(g.barRect, nullptr);
    return;
  }
  switch (act) {
    case ACT_THIN:
      g.logicalWidth = kWidthThin;
      SaveConfig();
      RefreshWidthVisuals();
      return;
    case ACT_MID:
      g.logicalWidth = kWidthMid;
      SaveConfig();
      RefreshWidthVisuals();
      return;
    case ACT_THICK:
      g.logicalWidth = kWidthThick;
      SaveConfig();
      RefreshWidthVisuals();
      return;
    case ACT_PEN:
      SetTool(Tool::Pen);
      return;
    case ACT_LINE:
      SetTool(Tool::Line);
      return;
    case ACT_RECT:
      SetTool(Tool::Rect);
      return;
    case ACT_MARKER:
      SetTool(Tool::Marker);
      return;
    case ACT_ERASER:
      SetTool(Tool::Eraser);
      return;
    case ACT_UNDO:
      Undo();
      return;
    case ACT_REDO:
      Redo();
      return;
    case ACT_CLEAR:
      ClearAll();
      return;
    case ACT_SAVE:
      SavePng();
      return;
    case ACT_EXIT:
      ExitDraw();
      return;
    default:
      return;
  }
}

void ChangeWidth(float factor) {
  g.logicalWidth = std::clamp(g.logicalWidth * factor, 1.5f, 36.f);
  RefreshWidthVisuals();
}

void OnMouseDown(HWND hwnd, int x, int y) {
  g.mouse = POINT{x, y};
  SetCapture(hwnd);
  if (BlocksDrawing(x, y)) {
    g.gesture = Gesture::Ui;
    g.pressed = HitTest(x, y);
    g.hover = g.pressed;
    PresentRegion(g.barRect, nullptr);
    return;
  }
  if (g.tool == Tool::Eraser) {
    g.gesture = Gesture::Erase;
    g.eraseUndoPushed = false;
    EraseAt(x, y, EraserRect());
    return;
  }
  g.gesture = Gesture::Draw;
  g.straight = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  g.live = Stroke{};
  g.live.tool = g.tool;
  g.live.color = kColors[g.colorIndex];
  g.live.width = ActiveWidth(g.tool);
  g.live.points.push_back(POINT{x, y});
  g.liveActive = true;
  PresentRegion(StrokeBounds(g.live), nullptr);
}

void OnMouseMove(int x, int y) {
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (g.w > 0 && x >= g.w) x = g.w - 1;
  if (g.h > 0 && y >= g.h) y = g.h - 1;
  RECT oldRing = g.tool == Tool::Eraser ? EraserRect() : RECT{0, 0, 0, 0};
  g.mouse = POINT{x, y};
  SetCursor(LoadCursor(nullptr, BlocksDrawing(x, y) ? IDC_ARROW : IDC_CROSS));

  if (g.gesture == Gesture::Ui) {
    int hit = HitTest(x, y);
    if (hit != g.hover) {
      g.hover = hit;
      PresentRegion(g.barRect, nullptr);
    }
    return;
  }
  if (g.gesture == Gesture::Erase) {
    if (!BlocksDrawing(x, y)) EraseAt(x, y, oldRing);
    else PresentRegion(UnionR(oldRing, EraserRect()), nullptr);
    return;
  }
  if (g.gesture == Gesture::Draw && g.liveActive) {
    RECT before = StrokeBounds(g.live);
    AppendPoint(x, y);
    if (g.live.points.size() > 4000) {
      POINT cont = g.live.points.back();
      CommitLive();
      g.liveActive = true;
      g.live.tool = g.tool == Tool::Pen && g.straight ? Tool::Pen : g.tool;
      g.live.color = kColors[g.colorIndex];
      g.live.width = ActiveWidth(g.live.tool);
      g.live.points.clear();
      g.live.points.push_back(cont);
      g.live.points.push_back(POINT{x, y});
      g.gesture = Gesture::Draw;
    }
    PresentRegion(UnionR(before, StrokeBounds(g.live)), nullptr);
    return;
  }
  int hit = HitTest(x, y);
  if (hit != g.hover) {
    g.hover = hit;
    PresentRegion(g.barRect, nullptr);
  }
  if (g.tool == Tool::Eraser) PresentRegion(UnionR(oldRing, EraserRect()), nullptr);
}

void OnMouseUp(int x, int y) {
  g.mouse = POINT{x, y};
  Gesture gesture = g.gesture;
  int pressed = g.pressed;
  g.gesture = Gesture::None;
  g.pressed = 0;
  if (gesture == Gesture::Draw) CommitLive();
  else if (gesture == Gesture::Ui && pressed != 0 && HitTest(x, y) == pressed) DoAction(pressed);
  if (g.overlay && GetCapture() == g.overlay) ReleaseCapture();
  if (!g.overlay) return;
  g.hover = HitTest(x, y);
  PresentRegion(g.barRect, nullptr);
}

void ClampClient(int& x, int& y) {
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (g.w > 0 && x >= g.w) x = g.w - 1;
  if (g.h > 0 && y >= g.h) y = g.h - 1;
}

void ShowDrawMenu(HWND hwnd) {
  HMENU menu = CreatePopupMenu();
  UINT undoFlags = MF_STRING;
  if (g.undo.empty() && !g.liveActive) undoFlags |= MF_GRAYED;
  UINT redoFlags = MF_STRING;
  if (g.redo.empty()) redoFlags |= MF_GRAYED;
  AppendMenuW(menu, undoFlags, ACT_UNDO, L"撤销\tCtrl+Z");
  AppendMenuW(menu, redoFlags, ACT_REDO, L"重做\tCtrl+Y");
  AppendMenuW(menu, MF_STRING, ACT_CLEAR, L"清空");
  AppendMenuW(menu, MF_STRING, ACT_SAVE, L"保存到桌面\tCtrl+S");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, ACT_EXIT, L"结束标注\tEsc");
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
  PostMessageW(hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);
}

void OnKeyDown(WPARAM key, LPARAM lp) {
  bool repeated = (lp & (1L << 30)) != 0;
  bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  if (key == VK_ESCAPE) {
    if (g.liveActive) {
      RECT bounds = StrokeBounds(g.live);
      g.liveActive = false;
      g.live.points.clear();
      g.gesture = Gesture::None;
      PresentRegion(bounds, nullptr);
    } else {
      ExitDraw();
    }
    return;
  }
  if (ctrl && (key == 'Z' || key == 'Y')) {
    if (key == 'Y' || shift) Redo();
    else Undo();
    return;
  }
  if (ctrl && key == 'S') {
    if (!repeated) SavePng();
    return;
  }
  if (ctrl || g.gesture != Gesture::None) return;
  int color = -1;
  if (key >= '1' && key <= '6') color = (int)key - '1';
  if (key >= VK_NUMPAD1 && key <= VK_NUMPAD6) color = (int)key - VK_NUMPAD1;
  if (color >= 0 && color < kColorCount) {
    g.colorIndex = color;
    SaveConfig();
    PresentRegion(g.barRect, nullptr);
    return;
  }
  if (key == VK_OEM_MINUS || key == VK_OEM_4) {
    ChangeWidth(1.f / 1.12f);
    return;
  }
  if (key == VK_OEM_PLUS || key == VK_OEM_6) {
    ChangeWidth(1.12f);
    return;
  }
  switch (key) {
    case 'P':
      SetTool(Tool::Pen);
      break;
    case 'L':
      SetTool(Tool::Line);
      break;
    case 'R':
      SetTool(Tool::Rect);
      break;
    case 'H':
      SetTool(Tool::Marker);
      break;
    case 'E':
      SetTool(Tool::Eraser);
      break;
    default:
      break;
  }
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      if (g.canvas.dc) PresentRegion(RECT{0, 0, g.w, g.h}, hdc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        SetCursor(LoadCursor(nullptr, BlocksDrawing(pt.x, pt.y) ? IDC_ARROW : IDC_CROSS));
        return TRUE;
      }
      break;
    case WM_LBUTTONDOWN: {
      int x = GET_X_LPARAM(lp);
      int y = GET_Y_LPARAM(lp);
      ClampClient(x, y);
      OnMouseDown(hwnd, x, y);
      return 0;
    }
    case WM_MOUSEMOVE: {
      int x = GET_X_LPARAM(lp);
      int y = GET_Y_LPARAM(lp);
      OnMouseMove(x, y);
      return 0;
    }
    case WM_LBUTTONUP: {
      int x = GET_X_LPARAM(lp);
      int y = GET_Y_LPARAM(lp);
      ClampClient(x, y);
      OnMouseUp(x, y);
      return 0;
    }
    case WM_RBUTTONDOWN:
      return 0;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
      if (g.gesture == Gesture::None) ShowDrawMenu(hwnd);
      return 0;
    case WM_MOUSEWHEEL: {
      int delta = GET_WHEEL_DELTA_WPARAM(wp);
      if (delta > 0) ChangeWidth(1.12f);
      else if (delta < 0) ChangeWidth(1.f / 1.12f);
      return 0;
    }
    case WM_KEYDOWN:
      OnKeyDown(wp, lp);
      return 0;
    case WM_SYSKEYDOWN:
      if (wp == VK_ESCAPE || (wp == VK_F4 && (GetKeyState(VK_MENU) & 0x8000))) ExitDraw();
      return 0;
    case WM_SYSCHAR:
    case WM_SYSCOMMAND:
      if (msg == WM_SYSCOMMAND && (wp & 0xFFF0) == SC_CLOSE) {
        ExitDraw();
        return 0;
      }
      if (msg == WM_SYSCOMMAND && (wp & 0xFFF0) == SC_KEYMENU) return 0;
      if (msg == WM_SYSCHAR) return 0;
      break;
    case WM_TIMER:
      if (wp == 1) {
        g.toast[0] = 0;
        KillTimer(hwnd, 1);
        PresentRegion(g.toastRect, nullptr);
      }
      return 0;
    case WM_COMMAND:
      DoAction(LOWORD(wp));
      return 0;
    case WM_CLOSE:
      ExitDraw();
      return 0;
    case WM_DESTROY:
      g.overlay = nullptr;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

Surface MakeSurface(HDC ref, int w, int h) {
  Surface s;
  if (w <= 0 || h <= 0) return s;
  s.dc = CreateCompatibleDC(ref);
  if (!s.dc) return s;
  s.bmp = CreateCompatibleBitmap(ref, w, h);
  if (!s.bmp) {
    DeleteDC(s.dc);
    s.dc = nullptr;
    return s;
  }
  s.old = SelectObject(s.dc, s.bmp);
  s.w = w;
  s.h = h;
  return s;
}

bool AllocSurfaces(HDC ref, int w, int h) {
  g.bg.Free();
  g.canvas.Free();
  g.back.Free();
  g.bg = MakeSurface(ref, w, h);
  g.canvas = MakeSurface(ref, w, h);
  g.back = MakeSurface(ref, w, h);
  if (!g.bg.ok() || !g.canvas.ok() || !g.back.ok()) {
    g.bg.Free();
    g.canvas.Free();
    g.back.Free();
    return false;
  }
  g.w = w;
  g.h = h;
  return true;
}

void PaintTestPage(HDC dc, int w, int h) {
  RECT rc{0, 0, w, h};
  HBRUSH bg = CreateSolidBrush(RGB(248, 249, 251));
  FillRect(dc, &rc, bg);
  DeleteObject(bg);

  HPEN grid = CreatePen(PS_SOLID, 1, RGB(226, 230, 236));
  HGDIOBJ oldPen = SelectObject(dc, grid);
  for (int x = 0; x < w; x += 40) {
    MoveToEx(dc, x, 0, nullptr);
    LineTo(dc, x, h);
  }
  for (int y = 0; y < h; y += 40) {
    MoveToEx(dc, 0, y, nullptr);
    LineTo(dc, w, y);
  }
  SelectObject(dc, oldPen);
  DeleteObject(grid);

  HPEN ink = CreatePen(PS_SOLID, 2, RGB(40, 44, 52));
  oldPen = SelectObject(dc, ink);
  HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
  Rectangle(dc, w * 14 / 100, h * 22 / 100, w * 42 / 100, h * 58 / 100);
  Ellipse(dc, w * 50 / 100, h * 22 / 100, w * 80 / 100, h * 62 / 100);
  MoveToEx(dc, w * 18 / 100, h * 70 / 100, nullptr);
  LineTo(dc, w * 40 / 100, h * 70 / 100);
  LineTo(dc, w * 52 / 100, h * 86 / 100);
  SelectObject(dc, oldBrush);
  SelectObject(dc, oldPen);
  DeleteObject(ink);

  HFONT font = CreateFontW(-26, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
  HGDIOBJ oldFont = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(70, 76, 86));
  const wchar_t* title = L"模拟 CAD 画面 — 可以在上面试画";
  TextOutW(dc, 36, 28, title, lstrlenW(title));
  SelectObject(dc, oldFont);
  DeleteObject(font);
}

void HideKeys() {
  g.keyQueue.clear();
  g.keyChips.clear();
  g.keyAlpha = 0;
  if (g.keyWnd) ShowWindow(g.keyWnd, SW_HIDE);
}

float CursorDpiScale() {
  POINT pt;
  GetCursorPos(&pt);
  HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
  HMODULE shcore = LoadLibraryW(L"Shcore.dll");
  float scale = 1.f;
  if (shcore) {
    using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(shcore, "GetDpiForMonitor"));
    if (fn) {
      UINT x = 96, y = 96;
      if (SUCCEEDED(fn(mon, 0, &x, &y)) && x >= 96) scale = x / 96.f;
    }
    FreeLibrary(shcore);
  }
  return scale;
}

std::wstring KeyLabel(DWORD vk) {
  if (vk >= 'A' && vk <= 'Z') return std::wstring(1, (wchar_t)vk);
  if (vk >= '0' && vk <= '9') return std::wstring(1, (wchar_t)vk);
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
    return std::wstring(L"Num") + (wchar_t)(L'0' + (vk - VK_NUMPAD0));
  if (vk >= VK_F1 && vk <= VK_F24) {
    wchar_t buf[8];
    FormatText(buf, L"F%d", (int)(vk - VK_F1 + 1));
    return buf;
  }
  switch (vk) {
    case VK_RETURN: return L"Enter";
    case VK_ESCAPE: return L"Esc";
    case VK_SPACE: return L"Space";
    case VK_TAB: return L"Tab";
    case VK_BACK: return L"Backspace";
    case VK_DELETE: return L"Delete";
    case VK_INSERT: return L"Insert";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_LEFT: return L"←";
    case VK_RIGHT: return L"→";
    case VK_UP: return L"↑";
    case VK_DOWN: return L"↓";
    case VK_PAUSE: return L"Pause";
    case VK_SNAPSHOT: return L"PrtSc";
    case VK_CAPITAL: return L"CapsLock";
    default: break;
  }
  UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
  if (!scan) return L"";
  LONG param = (LONG)(scan << 16);
  switch (vk) {
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_DIVIDE:
    case VK_NUMLOCK:
      param |= (1 << 24);
      break;
    default:
      break;
  }
  wchar_t name[64];
  if (GetKeyNameTextW(param, name, 64) > 0) return name;
  return L"";
}

std::wstring ChordText(DWORD vk) {
  switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
      return L"";
    default:
      break;
  }
  std::wstring name = KeyLabel(vk);
  if (name.empty()) return L"";
  std::wstring text;
  if (GetAsyncKeyState(VK_CONTROL) & 0x8000) text += L"Ctrl + ";
  if (GetAsyncKeyState(VK_MENU) & 0x8000) text += L"Alt + ";
  if (GetAsyncKeyState(VK_SHIFT) & 0x8000) text += L"Shift + ";
  if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) text += L"Win + ";
  text += name;
  return text;
}

void Premultiply(DWORD* px, int count) {
  for (int i = 0; i < count; ++i) {
    BYTE a = (BYTE)(px[i] >> 24);
    BYTE r = (BYTE)((px[i] >> 16) & 255);
    BYTE gc = (BYTE)((px[i] >> 8) & 255);
    BYTE b = (BYTE)(px[i] & 255);
    r = (BYTE)(r * a / 255);
    gc = (BYTE)(gc * a / 255);
    b = (BYTE)(b * a / 255);
    px[i] = ((DWORD)a << 24) | ((DWORD)r << 16) | ((DWORD)gc << 8) | b;
  }
}

void PresentKeys() {
  if (!g.keyWnd || g.keyChips.empty()) return;
  float dpi = CursorDpiScale();
  float fontPx = 22.f * dpi;
  int padX = (int)std::lround(14 * dpi);
  int gap = (int)std::lround(8 * dpi);
  int chipH = (int)std::lround(48 * dpi);
  int minW = (int)std::lround(48 * dpi);
  HDC screen = GetDC(nullptr);
  gp::Font yahei(L"Microsoft YaHei", fontPx, gp::FontStyleBold, gp::UnitPixel);
  gp::Font segoe(L"Segoe UI", fontPx, gp::FontStyleBold, gp::UnitPixel);
  const gp::Font* font = yahei.IsAvailable() ? &yahei : &segoe;
  std::vector<int> widths;
  {
    gp::Graphics measure(screen);
    for (const KeyChip& chip : g.keyChips) {
      gp::RectF box;
      measure.MeasureString(chip.text.c_str(), -1, font, gp::PointF(0, 0), &box);
      widths.push_back((std::max)(minW, (int)std::ceil(box.Width) + padX * 2));
    }
  }
  int wndW = gap;
  for (int w : widths) wndW += w + gap;
  int wndH = chipH + gap * 2;
  gp::Bitmap frame(wndW, wndH, PixelFormat32bppARGB);
  {
    gp::Graphics gfx(&frame);
    gfx.SetSmoothingMode(gp::SmoothingModeAntiAlias);
    gfx.SetTextRenderingHint(gp::TextRenderingHintAntiAliasGridFit);
    gfx.SetCompositingMode(gp::CompositingModeSourceOver);
    gfx.Clear(gp::Color(0, 0, 0, 0));
    gp::StringFormat fmt;
    fmt.SetAlignment(gp::StringAlignmentCenter);
    fmt.SetLineAlignment(gp::StringAlignmentCenter);
    fmt.SetFormatFlags(gp::StringFormatFlagsNoWrap);
    int x = gap;
    for (size_t i = 0; i < g.keyChips.size(); ++i) {
      bool latest = i + 1 == g.keyChips.size();
      RECT rc{x, gap, x + widths[i], gap + chipH};
      gp::GraphicsPath path;
      AddRoundRect(path, rc, 10.f * dpi);
      gp::SolidBrush fill(latest ? gp::Color(236, 32, 34, 38) : gp::Color(210, 32, 34, 38));
      gfx.FillPath(&fill, &path);
      gp::Pen edge(latest ? gp::Color(255, 255, 154, 64) : gp::Color(90, 255, 255, 255), latest ? 2.f : 1.f);
      gfx.DrawPath(&edge, &path);
      gp::SolidBrush text(gp::Color(255, 248, 248, 248));
      gfx.DrawString(g.keyChips[i].text.c_str(), -1, font,
                     gp::RectF((gp::REAL)rc.left, (gp::REAL)rc.top, (gp::REAL)(rc.right - rc.left),
                               (gp::REAL)(rc.bottom - rc.top)),
                     &fmt, &text);
      x += widths[i] + gap;
    }
  }
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = wndW;
  bi.bmiHeader.biHeight = -wndH;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dib || !bits) {
    if (dib) DeleteObject(dib);
    ReleaseDC(nullptr, screen);
    return;
  }
  gp::Rect lockRect(0, 0, wndW, wndH);
  gp::BitmapData data{};
  if (frame.LockBits(&lockRect, gp::ImageLockModeRead, PixelFormat32bppARGB, &data) == gp::Ok) {
    for (int y = 0; y < wndH; ++y) {
      auto* src = reinterpret_cast<DWORD*>(static_cast<BYTE*>(data.Scan0) + y * data.Stride);
      auto* dst = static_cast<DWORD*>(bits) + y * wndW;
      for (int x = 0; x < wndW; ++x) dst[x] = src[x];
    }
    frame.UnlockBits(&data);
  }
  Premultiply(static_cast<DWORD*>(bits), wndW * wndH);
  HDC mem = CreateCompatibleDC(screen);
  HGDIOBJ old = SelectObject(mem, dib);
  POINT cursor;
  GetCursorPos(&cursor);
  HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{sizeof(info)};
  if (!GetMonitorInfoW(mon, &info)) {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &info.rcWork, 0);
  }
  int margin = (int)std::lround(22 * dpi);
  POINT origin{info.rcWork.right - wndW - margin, info.rcWork.bottom - wndH - margin};
  SIZE size{wndW, wndH};
  POINT src{0, 0};
  BLENDFUNCTION blend{AC_SRC_OVER, 0, g.keyAlpha, AC_SRC_ALPHA};
  if (!IsWindowVisible(g.keyWnd)) ShowWindow(g.keyWnd, SW_SHOWNOACTIVATE);
  UpdateLayeredWindow(g.keyWnd, screen, &origin, &size, mem, &src, 0, &blend, ULW_ALPHA);
  SelectObject(mem, old);
  DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  SetWindowPos(g.keyWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void FlushKeys() {
  if (g.overlay || g.selectWnd) {
    g.keyQueue.clear();
    HideKeys();
    return;
  }
  bool changed = false;
  while (!g.keyQueue.empty()) {
    std::wstring text = g.keyQueue.front();
    g.keyQueue.erase(g.keyQueue.begin());
    if (!g.keyChips.empty() && g.keyChips.back().text == text) continue;
    g.keyChips.push_back(KeyChip{text});
    if (g.keyChips.size() > 6) g.keyChips.erase(g.keyChips.begin());
    changed = true;
  }
  if (g.keyChips.empty()) return;
  g.keyShownAt = GetTickCount();
  g.keyAlpha = 255;
  if (changed || !IsWindowVisible(g.keyWnd)) PresentKeys();
  else {
    g.keyAlpha = 255;
    PresentKeys();
  }
}

void TickKeys() {
  if (!g.keyWnd || !IsWindowVisible(g.keyWnd) || g.keyChips.empty()) return;
  DWORD age = GetTickCount() - g.keyShownAt;
  if (age < 1700) return;
  if (age >= 2100) {
    HideKeys();
    return;
  }
  g.keyAlpha = (BYTE)(255 * (2100 - age) / 400);
  PresentKeys();
}

void QueueKey(DWORD vk) {
  if (!g.host || g.overlay || g.selectWnd) return;
  std::wstring text = ChordText(vk);
  if (text.empty()) return;
  g.keyQueue.push_back(std::move(text));
  PostMessageW(g.host, WM_KEYSHOW, 0, 0);
}

LRESULT CALLBACK KeyHookProc(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
    auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
    if (info && !(info->flags & LLKHF_UP)) QueueKey(info->vkCode);
  }
  return CallNextHookEx(g.keyHook, code, wp, lp);
}

void StopKeys() {
  if (g.keyHook) {
    UnhookWindowsHookEx(g.keyHook);
    g.keyHook = nullptr;
  }
  HideKeys();
  if (g.keyWnd) {
    DestroyWindow(g.keyWnd);
    g.keyWnd = nullptr;
  }
}

LRESULT CALLBACK KeyProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  return DefWindowProcW(hwnd, msg, wp, lp);
}

RECT NormalizePts(POINT a, POINT b) {
  return RECT{(std::min)(a.x, b.x), (std::min)(a.y, b.y), (std::max)(a.x, b.x), (std::max)(a.y, b.y)};
}

void EndSelect() {
  HWND window = g.selectWnd;
  g.selectWnd = nullptr;
  g.shotDragging = false;
  if (window) DestroyWindow(window);
  g.shot.Free();
}

void PaintSelect(HDC target) {
  if (!g.shot.ok()) return;
  int w = g.shot.w;
  int h = g.shot.h;
  HDC mem = CreateCompatibleDC(target);
  HBITMAP buf = CreateCompatibleBitmap(target, w, h);
  HGDIOBJ old = SelectObject(mem, buf);
  BitBlt(mem, 0, 0, w, h, g.shot.dc, 0, 0, SRCCOPY);
  RECT sel = NormalizePts(g.shotA, g.shotB);
  bool show = g.shotDragging && sel.right - sel.left > 1 && sel.bottom - sel.top > 1;
  {
    gp::Graphics gfx(mem);
    gfx.SetSmoothingMode(gp::SmoothingModeAntiAlias);
    gfx.SetTextRenderingHint(gp::TextRenderingHintAntiAliasGridFit);
    gp::SolidBrush dim(gp::Color(96, 0, 0, 0));
    if (!show) {
      gfx.FillRectangle(&dim, 0, 0, w, h);
      gp::Font yahei(L"Microsoft YaHei", 28, gp::FontStyleRegular, gp::UnitPixel);
      gp::SolidBrush text(gp::Color(255, 255, 255, 255));
      gp::StringFormat fmt;
      fmt.SetAlignment(gp::StringAlignmentCenter);
      fmt.SetLineAlignment(gp::StringAlignmentCenter);
      gfx.DrawString(L"拖出要贴住的区域\nEsc 取消", -1, &yahei, gp::RectF(0, 0, (gp::REAL)w, (gp::REAL)h), &fmt,
                     &text);
    } else {
      int rw = sel.right - sel.left;
      int rh = sel.bottom - sel.top;
      if (sel.top > 0) gfx.FillRectangle(&dim, 0, 0, w, sel.top);
      if (sel.left > 0) gfx.FillRectangle(&dim, 0, sel.top, sel.left, rh);
      if (w - sel.right > 0) gfx.FillRectangle(&dim, sel.right, sel.top, w - sel.right, rh);
      if (h - sel.bottom > 0) gfx.FillRectangle(&dim, 0, sel.bottom, w, h - sel.bottom);
      gp::Pen pen(gp::Color(255, 255, 149, 48), 2.f);
      gfx.DrawRectangle(&pen, (gp::REAL)sel.left, (gp::REAL)sel.top, (gp::REAL)rw - 1, (gp::REAL)rh - 1);
      wchar_t sizeText[32];
      FormatText(sizeText, L"%d × %d", rw, rh);
      gp::Font font(L"Microsoft YaHei", 16, gp::FontStyleRegular, gp::UnitPixel);
      gp::SolidBrush plate(gp::Color(220, 24, 24, 24));
      gp::RectF plateRc((gp::REAL)sel.left, (gp::REAL)(sel.top > 28 ? sel.top - 28 : sel.top + 4), 110, 24);
      gfx.FillRectangle(&plate, plateRc);
      gp::SolidBrush white(gp::Color(255, 255, 255, 255));
      gfx.DrawString(sizeText, -1, &font, plateRc, nullptr, &white);
    }
  }
  BitBlt(target, 0, 0, w, h, mem, 0, 0, SRCCOPY);
  SelectObject(mem, old);
  DeleteObject(buf);
  DeleteDC(mem);
}

RECT PinCloseRect(HWND hwnd) {
  RECT rc{};
  GetClientRect(hwnd, &rc);
  return RECT{rc.right - 28, 6, rc.right - 6, 28};
}

void DestroyPinWindow(Pin* pin) {
  if (!pin || !pin->hwnd) return;
  HWND window = pin->hwnd;
  pin->hwnd = nullptr;
  DestroyWindow(window);
}

void CloseAllPins() {
  while (!g.pins.empty()) DestroyPinWindow(g.pins.back());
}

void CopyPinImage(Pin* pin) {
  if (!pin || !pin->bmp) return;
  HBITMAP copy = (HBITMAP)CopyImage(pin->bmp, IMAGE_BITMAP, 0, 0, 0);
  if (!copy || !OpenClipboard(pin->hwnd)) {
    if (copy) DeleteObject(copy);
    return;
  }
  EmptyClipboard();
  if (!SetClipboardData(CF_BITMAP, copy)) DeleteObject(copy);
  CloseClipboard();
}

void SavePinImage(Pin* pin) {
  if (!pin || !pin->bmp) return;
  std::wstring path = NewPicturePath();
  NotifySaved(!path.empty() && SaveHbitmapPng(pin->bmp, path));
}

LRESULT CALLBACK PinProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* pin = reinterpret_cast<Pin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rc{};
      GetClientRect(hwnd, &rc);
      HDC mem = CreateCompatibleDC(hdc);
      HBITMAP buf = CreateCompatibleBitmap(hdc, (std::max)(1L, rc.right), (std::max)(1L, rc.bottom));
      HGDIOBJ old = SelectObject(mem, buf);
      if (pin && pin->bmp) {
        HDC src = CreateCompatibleDC(hdc);
        HGDIOBJ oldBmp = SelectObject(src, pin->bmp);
        SetStretchBltMode(mem, HALFTONE);
        StretchBlt(mem, 0, 0, rc.right, rc.bottom, src, 0, 0, pin->imgW, pin->imgH, SRCCOPY);
        SelectObject(src, oldBmp);
        DeleteDC(src);
      }
      HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 140, 48));
      HGDIOBJ oldPen = SelectObject(mem, pen);
      HGDIOBJ oldBrush = SelectObject(mem, GetStockObject(HOLLOW_BRUSH));
      Rectangle(mem, 1, 1, rc.right - 1, rc.bottom - 1);
      SelectObject(mem, oldBrush);
      SelectObject(mem, oldPen);
      DeleteObject(pen);
      RECT box = PinCloseRect(hwnd);
      HBRUSH badge = CreateSolidBrush(RGB(32, 32, 32));
      HPEN whitePen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
      HGDIOBJ prevBrush = SelectObject(mem, badge);
      HGDIOBJ prevPen = SelectObject(mem, whitePen);
      Ellipse(mem, box.left, box.top, box.right, box.bottom);
      MoveToEx(mem, box.left + 7, box.top + 7, nullptr);
      LineTo(mem, box.right - 7, box.bottom - 7);
      MoveToEx(mem, box.right - 7, box.top + 7, nullptr);
      LineTo(mem, box.left + 7, box.bottom - 7);
      SelectObject(mem, prevBrush);
      SelectObject(mem, prevPen);
      DeleteObject(whitePen);
      DeleteObject(badge);
      BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old);
      DeleteObject(buf);
      DeleteDC(mem);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_LBUTTONDBLCLK:
      DestroyPinWindow(pin);
      return 0;
    case WM_LBUTTONDOWN: {
      if (!pin) return 0;
      RECT closeBox = PinCloseRect(hwnd);
      if (PtInRect(&closeBox, POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)})) {
        DestroyPinWindow(pin);
        return 0;
      }
      pin->dragging = true;
      pin->dragX = GET_X_LPARAM(lp);
      pin->dragY = GET_Y_LPARAM(lp);
      SetCapture(hwnd);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (pin && pin->dragging) {
        POINT pt;
        GetCursorPos(&pt);
        SetWindowPos(hwnd, nullptr, pt.x - pin->dragX, pt.y - pin->dragY, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
      return 0;
    case WM_LBUTTONUP:
      if (pin) pin->dragging = false;
      if (GetCapture() == hwnd) ReleaseCapture();
      return 0;
    case WM_MOUSEWHEEL:
      if (!pin || pin->imgW < 1 || pin->imgH < 1) return 0;
      {
        float factor = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1.12f : 1.f / 1.12f;
        float next = std::clamp(pin->scale * factor, 0.2f, 4.f);
        RECT wr{};
        GetWindowRect(hwnd, &wr);
        int cx = (wr.left + wr.right) / 2;
        int cy = (wr.top + wr.bottom) / 2;
        int nw = (std::max)(32, (int)std::lround(pin->imgW * next));
        int nh = (std::max)(32, (int)std::lround(pin->imgH * next));
        pin->scale = next;
        SetWindowPos(hwnd, HWND_TOPMOST, cx - nw / 2, cy - nh / 2, nw, nh, 0);
      }
      return 0;
    case WM_RBUTTONUP: {
      HMENU menu = CreatePopupMenu();
      AppendMenuW(menu, MF_STRING, PIN_CLOSE, L"关闭这张贴图");
      AppendMenuW(menu, MF_STRING, PIN_CLOSE_ALL, L"关闭全部贴图");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING, PIN_COPY, L"复制");
      AppendMenuW(menu, MF_STRING, PIN_SAVE, L"保存到图片\\画笔");
      POINT pt;
      GetCursorPos(&pt);
      SetForegroundWindow(hwnd);
      TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
      PostMessageW(hwnd, WM_NULL, 0, 0);
      DestroyMenu(menu);
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case PIN_CLOSE:
          DestroyPinWindow(pin);
          break;
        case PIN_CLOSE_ALL:
          CloseAllPins();
          break;
        case PIN_COPY:
          CopyPinImage(pin);
          break;
        case PIN_SAVE:
          SavePinImage(pin);
          break;
        default:
          break;
      }
      return 0;
    case WM_KEYDOWN:
      if (wp == VK_ESCAPE) DestroyPinWindow(pin);
      return 0;
    case WM_DESTROY: {
      Pin* dying = reinterpret_cast<Pin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      if (dying) {
        if (dying->bmp) DeleteObject(dying->bmp);
        dying->bmp = nullptr;
        dying->hwnd = nullptr;
        auto it = std::find(g.pins.begin(), g.pins.end(), dying);
        if (it != g.pins.end()) g.pins.erase(it);
        delete dying;
      }
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void CreatePin(HBITMAP bmp, int imgW, int imgH, int x, int y) {
  if (!bmp || imgW < 1 || imgH < 1) {
    if (bmp) DeleteObject(bmp);
    return;
  }
  POINT anchor{x + imgW / 2, y + imgH / 2};
  HMONITOR mon = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{sizeof(info)};
  GetMonitorInfoW(mon, &info);
  int maxW = (int)(info.rcWork.right - info.rcWork.left) * 4 / 5;
  int maxH = (int)(info.rcWork.bottom - info.rcWork.top) * 4 / 5;
  if (maxW < 80) maxW = 80;
  if (maxH < 80) maxH = 80;
  float scale = 1.f;
  if (imgW > maxW || imgH > maxH)
    scale = (std::min)(maxW / (float)imgW, maxH / (float)imgH);
  int wndW = (std::max)(32, (int)std::lround(imgW * scale));
  int wndH = (std::max)(32, (int)std::lround(imgH * scale));
  auto* pin = new Pin();
  pin->bmp = bmp;
  pin->imgW = imgW;
  pin->imgH = imgH;
  pin->scale = scale;
  pin->hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"HuaBiPin", L"贴图", WS_POPUP, x, y, wndW,
                              wndH, nullptr, nullptr, g.hinst, nullptr);
  if (!pin->hwnd) {
    DeleteObject(bmp);
    delete pin;
    return;
  }
  SetWindowLongPtrW(pin->hwnd, GWLP_USERDATA, (LONG_PTR)pin);
  g.pins.push_back(pin);
  ShowWindow(pin->hwnd, SW_SHOW);
  SetWindowPos(pin->hwnd, HWND_TOPMOST, x, y, wndW, wndH, SWP_SHOWWINDOW);
}

HBITMAP CropShot(RECT sel) {
  int rw = sel.right - sel.left;
  int rh = sel.bottom - sel.top;
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  HBITMAP bmp = CreateCompatibleBitmap(screen, rw, rh);
  HGDIOBJ old = SelectObject(mem, bmp);
  BitBlt(mem, 0, 0, rw, rh, g.shot.dc, sel.left, sel.top, SRCCOPY);
  SelectObject(mem, old);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  return bmp;
}

void FinishSelect() {
  RECT sel = NormalizePts(g.shotA, g.shotB);
  int rw = sel.right - sel.left;
  int rh = sel.bottom - sel.top;
  if (rw < 8 || rh < 8 || !g.shot.ok()) {
    EndSelect();
    return;
  }
  HBITMAP bmp = CropShot(sel);
  int x = g.shotX + sel.left;
  int y = g.shotY + sel.top;
  EndSelect();
  CreatePin(bmp, rw, rh, x, y);
}

LRESULT CALLBACK SelectProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      PaintSelect(hdc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_SETCURSOR:
      SetCursor(LoadCursor(nullptr, IDC_CROSS));
      return TRUE;
    case WM_LBUTTONDOWN:
      g.shotDragging = true;
      g.shotA = POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      g.shotB = g.shotA;
      SetCapture(hwnd);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_MOUSEMOVE:
      if (g.shotDragging) {
        g.shotB = POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_LBUTTONUP:
      if (GetCapture() == hwnd) ReleaseCapture();
      if (g.shotDragging) {
        g.shotB = POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        FinishSelect();
      }
      return 0;
    case WM_RBUTTONUP:
    case WM_KEYDOWN:
      if (msg == WM_RBUTTONUP || wp == VK_ESCAPE) EndSelect();
      return 0;
    case WM_DESTROY:
      if (g.selectWnd == hwnd) g.selectWnd = nullptr;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void StartPin() {
  if (g.overlay || g.selectWnd) return;
  HideKeys();
  POINT cursor;
  GetCursorPos(&cursor);
  HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{sizeof(info)};
  if (!GetMonitorInfoW(mon, &info)) return;
  RECT area = info.rcMonitor;
  int w = area.right - area.left;
  int h = area.bottom - area.top;
  HDC screen = GetDC(nullptr);
  g.shot.Free();
  g.shot = MakeSurface(screen, w, h);
  if (!g.shot.ok() || !BitBlt(g.shot.dc, 0, 0, w, h, screen, area.left, area.top, SRCCOPY | CAPTUREBLT)) {
    if (g.shot.ok()) BitBlt(g.shot.dc, 0, 0, w, h, screen, area.left, area.top, SRCCOPY);
  }
  ReleaseDC(nullptr, screen);
  if (!g.shot.ok()) {
    MessageBoxW(nullptr, L"无法截取屏幕。", kAppTitle, MB_OK | MB_ICONERROR);
    return;
  }
  g.shotX = area.left;
  g.shotY = area.top;
  g.shotDragging = false;
  g.selectWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"HuaBiSelect", L"贴图", WS_POPUP, area.left,
                                area.top, w, h, nullptr, nullptr, g.hinst, nullptr);
  if (!g.selectWnd) {
    g.shot.Free();
    MessageBoxW(nullptr, L"无法开始贴图。", kAppTitle, MB_OK | MB_ICONERROR);
    return;
  }
  ShowWindow(g.selectWnd, SW_SHOW);
  SetWindowPos(g.selectWnd, HWND_TOPMOST, area.left, area.top, w, h, SWP_SHOWWINDOW);
  SetForegroundWindow(g.selectWnd);
}

HBITMAP BitmapFromClipboard() {
  if (!OpenClipboard(nullptr)) return nullptr;
  HBITMAP result = nullptr;
  HBITMAP cbmp = (HBITMAP)GetClipboardData(CF_BITMAP);
  if (cbmp) result = (HBITMAP)CopyImage(cbmp, IMAGE_BITMAP, 0, 0, 0);
  if (!result) {
    HANDLE packed = GetClipboardData(CF_DIB);
    if (packed) {
      auto* info = reinterpret_cast<BITMAPINFO*>(GlobalLock(packed));
      if (info) {
        const BITMAPINFOHEADER& head = info->bmiHeader;
        BYTE* pixels = reinterpret_cast<BYTE*>(info) + head.biSize;
        if (head.biBitCount <= 8) {
          int colors = head.biClrUsed ? (int)head.biClrUsed : (1 << head.biBitCount);
          pixels += colors * (int)sizeof(RGBQUAD);
        } else if (head.biCompression == BI_BITFIELDS && head.biSize == sizeof(BITMAPINFOHEADER)) {
          pixels += 12;
        }
        HDC dc = GetDC(nullptr);
        result = CreateDIBitmap(dc, &head, CBM_INIT, pixels, info, DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        GlobalUnlock(packed);
      }
    }
  }
  CloseClipboard();
  return result;
}

void PinClipboard() {
  if (g.overlay || g.selectWnd) return;
  HBITMAP bmp = BitmapFromClipboard();
  if (!bmp) {
    MessageBoxW(nullptr, L"剪贴板里没有图片。可以先复制一张图，或用 Ctrl+Alt+Q 框选。", kAppTitle,
                MB_OK | MB_ICONINFORMATION);
    return;
  }
  BITMAP bm{};
  if (!GetObjectW(bmp, sizeof(bm), &bm) || bm.bmWidth < 1 || bm.bmHeight < 1) {
    DeleteObject(bmp);
    return;
  }
  POINT cursor;
  GetCursorPos(&cursor);
  int imgH = bm.bmHeight < 0 ? -bm.bmHeight : bm.bmHeight;
  CreatePin(bmp, bm.bmWidth, imgH, cursor.x - bm.bmWidth / 4, cursor.y - imgH / 4);
}

void EnterDraw() {
  if (g.overlay) return;
  if (g.selectWnd) EndSelect();
  HideKeys();
  g.strokes.clear();
  g.undo.clear();
  g.redo.clear();
  g.live = Stroke{};
  g.liveActive = false;
  g.gesture = Gesture::None;
  g.pressed = 0;
  g.hover = 0;
  g.toast[0] = 0;
  g.eraseUndoPushed = false;

  HDC screen = GetDC(nullptr);
  bool ready = false;
  if (g.testMode) {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = (std::min)(1100, (int)(work.right - work.left) - 80);
    int h = (std::min)(700, (int)(work.bottom - work.top) - 80);
    if (w < 480) w = (std::max)(480, (int)(work.right - work.left) - 20);
    if (h < 360) h = (std::max)(360, (int)(work.bottom - work.top) - 20);
    int x = work.left + ((work.right - work.left) - w) / 2;
    int y = work.top + ((work.bottom - work.top) - h) / 2;
    if (AllocSurfaces(screen, w, h)) {
      PaintTestPage(g.bg.dc, w, h);
      BitBlt(g.canvas.dc, 0, 0, w, h, g.bg.dc, 0, 0, SRCCOPY);
      g.vx = x;
      g.vy = y;
      ready = true;
    }
  } else {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    auto capture = [&](int cx, int cy, int cw, int ch) {
      if (!AllocSurfaces(screen, cw, ch)) return false;
      if (!BitBlt(g.bg.dc, 0, 0, cw, ch, screen, cx, cy, SRCCOPY | CAPTUREBLT))
        BitBlt(g.bg.dc, 0, 0, cw, ch, screen, cx, cy, SRCCOPY);
      BitBlt(g.canvas.dc, 0, 0, cw, ch, g.bg.dc, 0, 0, SRCCOPY);
      g.vx = cx;
      g.vy = cy;
      return true;
    };
    ready = capture(x, y, w, h);
    if (!ready) {
      POINT pt;
      GetCursorPos(&pt);
      HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
      MONITORINFO info{sizeof(info)};
      if (GetMonitorInfoW(mon, &info)) {
        RECT m = info.rcMonitor;
        ready = capture(m.left, m.top, m.right - m.left, m.bottom - m.top);
      }
    }
  }
  ReleaseDC(nullptr, screen);
  if (!ready) {
    MessageBoxW(nullptr, L"无法定格屏幕。可以关掉一些占内存的程序后再试。", kAppTitle,
                MB_OK | MB_ICONERROR);
    return;
  }

  RememberForeground();
  UpdateDpi(RECT{g.vx, g.vy, g.vx + g.w, g.vy + g.h});
  g.overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"HuaBiOverlay", kAppTitle, WS_POPUP,
                              g.vx, g.vy, g.w, g.h, nullptr, nullptr, g.hinst, nullptr);
  if (!g.overlay) {
    g.bg.Free();
    g.canvas.Free();
    g.back.Free();
    MessageBoxW(nullptr, L"无法打开画布。", kAppTitle, MB_OK | MB_ICONERROR);
    return;
  }
  LayoutUi();
  ShowWindow(g.overlay, SW_SHOW);
  SetWindowPos(g.overlay, HWND_TOPMOST, g.vx, g.vy, g.w, g.h, SWP_SHOWWINDOW);
  FocusOverlay();
  SyncMouse();
  PresentFull();
}

void ExitDraw() {
  if (!g.overlay) return;
  HWND window = g.overlay;
  KillTimer(window, 1);
  HWND restore = g.prevFore;
  g.prevFore = nullptr;
  DestroyWindow(window);
  g.bg.Free();
  g.canvas.Free();
  g.back.Free();
  g.strokes.clear();
  g.undo.clear();
  g.redo.clear();
  g.live = Stroke{};
  g.liveActive = false;
  g.gesture = Gesture::None;
  g.pressed = 0;
  g.hover = 0;
  g.items.clear();
  g.toast[0] = 0;
  g.w = 0;
  g.h = 0;
  SaveConfig();
  if (restore && restore != g.host && IsWindow(restore)) SetForegroundWindow(restore);
}

void ToggleDraw() {
  DWORD now = GetTickCount();
  if (now - g.lastToggle < 280) return;
  g.lastToggle = now;
  if (g.overlay) ExitDraw();
  else EnterDraw();
}

void SaveConfig() {
  if (g.iniPath.empty()) return;
  wchar_t buf[64];
  FormatText(buf, L"%d", g.hotkeyIndex);
  WritePrivateProfileStringW(L"pen", L"hotkey", buf, g.iniPath.c_str());
  FormatText(buf, L"%d", g.colorIndex);
  WritePrivateProfileStringW(L"pen", L"color", buf, g.iniPath.c_str());
  FormatText(buf, L"%d", (int)g.tool);
  WritePrivateProfileStringW(L"pen", L"tool", buf, g.iniPath.c_str());
  FormatText(buf, L"%.2f", g.logicalWidth);
  WritePrivateProfileStringW(L"pen", L"width", buf, g.iniPath.c_str());
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, g.iniPath.c_str());
}

void LoadConfig() {
  g.hotkeyIndex = (int)GetPrivateProfileIntW(L"pen", L"hotkey", 0, g.iniPath.c_str());
  if (g.hotkeyIndex < 0 || g.hotkeyIndex >= kHotkeyCount) g.hotkeyIndex = 0;
  g.colorIndex = (int)GetPrivateProfileIntW(L"pen", L"color", 0, g.iniPath.c_str());
  if (g.colorIndex < 0 || g.colorIndex >= kColorCount) g.colorIndex = 0;
  int tool = (int)GetPrivateProfileIntW(L"pen", L"tool", 0, g.iniPath.c_str());
  if (tool < 0 || tool > (int)Tool::Eraser) tool = 0;
  g.tool = (Tool)tool;
  wchar_t buf[32];
  GetPrivateProfileStringW(L"pen", L"width", L"4", buf, 32, g.iniPath.c_str());
  g.logicalWidth = wcstof(buf, nullptr);
  if (!(g.logicalWidth >= 1.5f && g.logicalWidth <= 36.f)) g.logicalWidth = kWidthThin;
}

void RefreshTexts() {
  const wchar_t* label = kHotkeys[g.hotkeyIndex].label;
  std::wstring hint = std::wstring(L"按 ") + label + L" 或 Esc 结束";
  std::wstring tip = std::wstring(kAppTitle) + L" — " + label + L" 开始/结束，右键可更换";
  CopyText(g.hintBuf, hint.c_str());
  CopyText(g.tipBuf, tip.c_str());
  UpdateTip();
}

void UpdateTip() {
  if (!g.trayAdded) return;
  g.nid.uFlags = NIF_TIP;
  CopyText(g.nid.szTip, g.tipBuf);
  Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

bool TryHotkey(int index) {
  if (!g.host || index < 0 || index >= kHotkeyCount) return false;
  UnregisterHotKey(g.host, HOTKEY_ID);
  const Hotkey& next = kHotkeys[index];
  if (RegisterHotKey(g.host, HOTKEY_ID, next.mods | MOD_NOREPEAT, next.vk)) {
    g.hotkeyIndex = index;
    RefreshTexts();
    return true;
  }
  if (index != g.hotkeyIndex) {
    const Hotkey& prev = kHotkeys[g.hotkeyIndex];
    RegisterHotKey(g.host, HOTKEY_ID, prev.mods | MOD_NOREPEAT, prev.vk);
  }
  return false;
}

void ShowHelp() {
  std::wstring text =
      std::wstring(L"36953画笔在后台运行，用来上课时临时在屏幕上圈画，并让学生看见你按了什么键。\r\n") +
      L"画面会定格，结束之后回到原来的软件，笔迹不会写进 CAD。\r\n" + L"\r\n" + L"开始 / 结束：" +
      kHotkeys[g.hotkeyIndex].label + L"\r\n" +
      L"右键托盘图标可以更换快捷键。\r\n" + L"\r\n" +
      L"屏幕贴图：Ctrl+Alt+Q，拖出一个区域，松手后这块图会浮在最前面。\r\n" +
      L"可以拖动，滚轮缩放，双击或点右上角关闭。右键还能复制、保存、关闭全部。\r\n" +
      L"剪贴板里已有图片时，也可以从托盘菜单直接贴上。\r\n" + L"\r\n" +
      L"按键显示在当前屏幕右下角，避开中间的图纸和 CAD 底部命令行。\r\n" +
      L"定格涂画或正在框选贴图时会自动隐藏，结束之后恢复。\r\n" + L"\r\n" +
      L"Esc          结束标注\r\n" + L"Ctrl+Z       撤销      Ctrl+Y 重做\r\n" +
      L"Ctrl+S       保存到“图片\\画笔”文件夹\r\n" + L"滚轮         调节粗细\r\n" +
      L"按住 Shift   临时画直线\r\n" + L"P 画笔   L 直线   R 矩形   H 荧光   E 橡皮\r\n" +
      L"1 到 6     红、黄、绿、蓝、白、黑\r\n" +
      L"打开后默认是细的红画笔。红色、细、画笔都排在各自一排的最前面。\r\n" + L"\r\n" +
      L"橡皮会去掉碰到的整条线。\r\n" +
      L"双击托盘图标也能开始。右键图标里的「退出程序」才会真正关掉。\r\n" + L"\r\n" +
      L"软件版本：V1.0\r\n" + L"作者：36953\r\n" +
      L"用下来如果有好的建议，欢迎发邮件告诉我：36593@163.com";
  MessageBoxW(g.overlay ? g.overlay : nullptr, text.c_str(), kAppTitle,
              MB_OK | MB_ICONINFORMATION);
}

void ShowTrayMenu(HWND hwnd) {
  HMENU menu = CreatePopupMenu();
  HMENU keys = CreatePopupMenu();
  std::wstring toggle = std::wstring(L"开始 / 结束\t") + kHotkeys[g.hotkeyIndex].label;
  AppendMenuW(menu, MF_STRING, ID_TOGGLE, toggle.c_str());
  AppendMenuW(menu, MF_STRING, ID_PIN, L"屏幕贴图\tCtrl+Alt+Q");
  AppendMenuW(menu, MF_STRING, ID_PIN_CLIP, L"贴上剪贴板图片");
  AppendMenuW(menu, MF_STRING, ID_HELP, L"使用说明");
  AppendMenuW(menu, MF_POPUP, (UINT_PTR)keys, L"更换快捷键");
  for (int i = 0; i < kHotkeyCount; ++i)
    AppendMenuW(keys, MF_STRING, ID_HK_BASE + i, kHotkeys[i].menu);
  CheckMenuRadioItem(keys, ID_HK_BASE, ID_HK_BASE + kHotkeyCount - 1, ID_HK_BASE + g.hotkeyIndex,
                     MF_BYCOMMAND);
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, ID_QUIT, L"退出程序");
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, nullptr);
  PostMessageW(hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);
}

void ShutdownExtras() {
  EndSelect();
  CloseAllPins();
  StopKeys();
  if (g.host && IsWindow(g.host)) UnregisterHotKey(g.host, HOTKEY_PIN);
}

void Quit() {
  if (g.quitting) return;
  g.quitting = true;
  SaveConfig();
  ExitDraw();
  ShutdownExtras();
  if (g.host && IsWindow(g.host)) UnregisterHotKey(g.host, HOTKEY_ID);
  if (g.trayAdded) {
    Shell_NotifyIconW(NIM_DELETE, &g.nid);
    g.trayAdded = false;
  }
  if (g.host && IsWindow(g.host)) DestroyWindow(g.host);
}

LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_TRAYICON:
      if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
        RememberForeground();
        ShowTrayMenu(hwnd);
      } else if (lp == WM_LBUTTONDBLCLK) {
        RememberForeground();
        ToggleDraw();
      }
      return 0;
    case WM_COMMAND: {
      int id = LOWORD(wp);
      if (id == ID_TOGGLE) ToggleDraw();
      else if (id == ID_PIN) StartPin();
      else if (id == ID_PIN_CLIP) PinClipboard();
      else if (id == ID_HELP) ShowHelp();
      else if (id == ID_QUIT) Quit();
      else if (id >= ID_HK_BASE && id < ID_HK_BASE + kHotkeyCount) {
        int index = id - ID_HK_BASE;
        if (!TryHotkey(index)) {
          MessageBoxW(nullptr, L"这个快捷键被别的软件占用了，换一个。", kAppTitle, MB_OK | MB_ICONWARNING);
        } else {
          SaveConfig();
          if (g.overlay) PresentFull();
        }
      }
      return 0;
    }
    case WM_HOTKEY:
      if (wp == HOTKEY_ID) ToggleDraw();
      else if (wp == HOTKEY_PIN) StartPin();
      return 0;
    case WM_KEYSHOW:
      FlushKeys();
      return 0;
    case WM_TIMER:
      if (wp == KEY_TIMER) TickKeys();
      return 0;
    case WM_TOGGLE:
      ToggleDraw();
      return 0;
    case WM_CLOSE:
      Quit();
      return 0;
    case WM_ENDSESSION:
      if (g.trayAdded) Shell_NotifyIconW(NIM_DELETE, &g.nid);
      g.trayAdded = false;
      return 0;
    case WM_DESTROY:
      g.host = nullptr;
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

HICON CreateBrushIcon() {
  const int s = 32;
  BITMAPV5HEADER bi{};
  bi.bV5Size = sizeof(bi);
  bi.bV5Width = s;
  bi.bV5Height = -s;
  bi.bV5Planes = 1;
  bi.bV5BitCount = 32;
  bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask = 0x00FF0000;
  bi.bV5GreenMask = 0x0000FF00;
  bi.bV5BlueMask = 0x000000FF;
  bi.bV5AlphaMask = 0xFF000000;

  HDC screen = GetDC(nullptr);
  void* bits = nullptr;
  HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits,
                                  nullptr, 0);
  ReleaseDC(nullptr, screen);
  if (!color || !bits) return LoadIcon(nullptr, IDI_APPLICATION);

  DWORD* px = static_cast<DWORD*>(bits);
  std::fill(px, px + s * s, 0u);
  for (int y = 0; y < s; ++y) {
    for (int x = 0; x < s; ++x) {
      float dx = x - 15.5f;
      float dy = y - 15.5f;
      float dist = std::sqrt(dx * dx + dy * dy);
      if (dist > 14.f) continue;
      float cover = dist > 12.6f ? (14.f - dist) / 1.4f : 1.f;
      int a = (int)std::clamp(cover * 255.f, 0.f, 255.f);
      int r = 214 * a / 255;
      int gc = 52 * a / 255;
      int b = 46 * a / 255;
      px[y * s + x] = ((DWORD)a << 24) | ((DWORD)r << 16) | ((DWORD)gc << 8) | (DWORD)b;
    }
  }
  for (int i = 0; i <= 16; ++i) {
    int x = 8 + i * 14 / 16;
    int y = 23 - i * 14 / 16;
    for (int oy = -1; oy <= 1; ++oy) {
      for (int ox = -1; ox <= 1; ++ox) {
        int pxX = x + ox;
        int pxY = y + oy;
        if (pxX < 0 || pxY < 0 || pxX >= s || pxY >= s) continue;
        px[pxY * s + pxX] = 0xFFFFFFFFu;
      }
    }
  }

  std::vector<BYTE> zero((size_t)(s * s / 8), 0);
  HBITMAP mask = CreateBitmap(s, s, 1, 1, zero.data());
  ICONINFO info{};
  info.fIcon = TRUE;
  info.hbmColor = color;
  info.hbmMask = mask;
  HICON icon = CreateIconIndirect(&info);
  DeleteObject(color);
  DeleteObject(mask);
  if (!icon) return LoadIcon(nullptr, IDI_APPLICATION);
  return icon;
}

void EnableDpi() {
  HMODULE user = GetModuleHandleW(L"user32.dll");
  auto setCtx = reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(
      GetProcAddress(user, "SetProcessDpiAwarenessContext"));
  if (setCtx) {
    if (setCtx(reinterpret_cast<HANDLE>((LONG_PTR)-4))) return;
    if (setCtx(reinterpret_cast<HANDLE>((LONG_PTR)-3))) return;
  }
  SetProcessDPIAware();
}

void Cleanup() {
  ExitDraw();
  ShutdownExtras();
  if (g.host && IsWindow(g.host)) UnregisterHotKey(g.host, HOTKEY_ID);
  if (g.trayAdded) {
    Shell_NotifyIconW(NIM_DELETE, &g.nid);
    g.trayAdded = false;
  }
  if (g.icon) {
    DestroyIcon(g.icon);
    g.icon = nullptr;
  }
  if (g.mutex) {
    CloseHandle(g.mutex);
    g.mutex = nullptr;
  }
}

int Run() {
  wchar_t path[1024];
  GetModuleFileNameW(nullptr, path, 1024);
  wchar_t* slash = wcsrchr(path, L'\\');
  if (slash) *(slash + 1) = 0;
  else path[0] = 0;
  g.iniPath = std::wstring(path) + L"huabi.ini";
  LoadConfig();
  PicturesPenDir();

  const wchar_t* cmd = GetCommandLineW();
  g.testMode = cmd && wcsstr(cmd, L"--test") != nullptr;

  g.icon = CreateBrushIcon();

  WNDCLASSW hostCls{};
  hostCls.lpfnWndProc = HostProc;
  hostCls.hInstance = g.hinst;
  hostCls.lpszClassName = L"HuaBiHost";
  hostCls.hIcon = g.icon;
  if (!RegisterClassW(&hostCls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

  WNDCLASSW overCls{};
  overCls.lpfnWndProc = OverlayProc;
  overCls.hInstance = g.hinst;
  overCls.lpszClassName = L"HuaBiOverlay";
  overCls.hCursor = LoadCursor(nullptr, IDC_CROSS);
  overCls.hIcon = g.icon;
  overCls.style = CS_HREDRAW | CS_VREDRAW;
  if (!RegisterClassW(&overCls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

  WNDCLASSW pinCls{};
  pinCls.lpfnWndProc = PinProc;
  pinCls.hInstance = g.hinst;
  pinCls.lpszClassName = L"HuaBiPin";
  pinCls.hCursor = LoadCursor(nullptr, IDC_ARROW);
  pinCls.hIcon = g.icon;
  if (!RegisterClassW(&pinCls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

  WNDCLASSW selectCls{};
  selectCls.lpfnWndProc = SelectProc;
  selectCls.hInstance = g.hinst;
  selectCls.lpszClassName = L"HuaBiSelect";
  selectCls.hCursor = LoadCursor(nullptr, IDC_CROSS);
  selectCls.hIcon = g.icon;
  if (!RegisterClassW(&selectCls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

  WNDCLASSW keyCls{};
  keyCls.lpfnWndProc = KeyProc;
  keyCls.hInstance = g.hinst;
  keyCls.lpszClassName = L"HuaBiKeys";
  keyCls.hCursor = LoadCursor(nullptr, IDC_ARROW);
  if (!RegisterClassW(&keyCls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

  g.host = CreateWindowExW(0, L"HuaBiHost", kAppTitle, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, g.hinst,
                           nullptr);
  if (!g.host) return 1;

  RefreshTexts();
  int prefer = g.hotkeyIndex;
  bool gotKey = false;
  for (int n = 0; n < kHotkeyCount; ++n) {
    if (TryHotkey((prefer + n) % kHotkeyCount)) {
      gotKey = true;
      break;
    }
  }
  if (!gotKey) {
    CopyText(g.hintBuf, L"按 Esc 结束");
    CopyText(g.tipBuf, L"36953画笔 — 双击图标开始（快捷键注册失败）");
  }
  SaveConfig();

  g.nid.cbSize = sizeof(g.nid);
  g.nid.hWnd = g.host;
  g.nid.uID = 1;
  g.nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_INFO;
  g.nid.uCallbackMessage = WM_TRAYICON;
  g.nid.hIcon = g.icon;
  CopyText(g.nid.szTip, g.tipBuf);
  CopyText(g.nid.szInfoTitle, kAppTitle);
  if (gotKey) {
    std::wstring info = std::wstring(L"按 ") + kHotkeys[g.hotkeyIndex].label +
                        L" 定格涂画。Ctrl+Alt+Q 贴图。按键显示在右下角，定格时自动隐藏。";
    CopyText(g.nid.szInfo, info.c_str());
  } else {
    CopyText(g.nid.szInfo, L"快捷键没有注册成功。请双击托盘图标开始画，或右键更换快捷键。");
  }
  g.nid.hBalloonIcon = g.icon;
  g.nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON | NIIF_NOSOUND;
  g.nid.uTimeout = 8000;
  if (Shell_NotifyIconW(NIM_ADD, &g.nid)) g.trayAdded = true;
  else MessageBoxW(nullptr, L"托盘图标创建失败，仍可使用快捷键。", kAppTitle, MB_OK | MB_ICONWARNING);

  g.keyWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                             L"HuaBiKeys", L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, g.hinst, nullptr);
  g.keyHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyHookProc, GetModuleHandleW(nullptr), 0);
  RegisterHotKey(g.host, HOTKEY_PIN, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'Q');
  SetTimer(g.host, KEY_TIMER, 80, nullptr);

  if (g.testMode) EnterDraw();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR, int) {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\HuaBi.Annotator.V1");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (mutex) CloseHandle(mutex);
    HWND existing = FindWindowW(L"HuaBiHost", kAppTitle);
    if (existing) PostMessageW(existing, WM_TOGGLE, 0, 0);
    return 0;
  }
  g.mutex = mutex;
  g.hinst = hinst;
  EnableDpi();

  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  g.needCo = SUCCEEDED(hr);
  gp::GdiplusStartupInput input;
  if (gp::GdiplusStartup(&g.gdip, &input, nullptr) != gp::Ok) {
    MessageBoxW(nullptr, L"GDI+ 初始化失败。", kAppTitle, MB_OK | MB_ICONERROR);
    if (g.needCo) CoUninitialize();
    if (g.mutex) CloseHandle(g.mutex);
    return 1;
  }

  int code = Run();
  Cleanup();
  if (g.gdip) gp::GdiplusShutdown(g.gdip);
  if (g.needCo) CoUninitialize();
  return code;
}
