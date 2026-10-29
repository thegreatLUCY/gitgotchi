#include <cli/cli.h>
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#if __has_include(<toolbox/cli/cli_registry.h>)
#define PipCli CliRegistry
#define PipCliStream PipeSide
#define pip_cli_add cli_registry_add_command
#define pip_cli_delete cli_registry_delete_command
#else
#define PipCli Cli
#define PipCliStream Cli
#define pip_cli_add cli_add_command
#define pip_cli_delete cli_delete_command
#endif
#include <stdio.h>
#include <stdlib.h>
#include <storage/storage.h>
#include <string.h>

#define SAVE_PATH EXT_PATH("apps_data/gitgotchi/pip.dat")
typedef enum { Idle, Walk, Pet, Eat, Sleep, Play } Mood;
typedef struct {
  FuriMutex *mutex;
  FuriMessageQueue *inputs;
  int x, target;
  uint32_t frame, until, bond, meals, rng;
  Mood mood;
  uint8_t screen[1024];
} Pip;
typedef struct {
  uint32_t magic, bond, meals;
} Save;
static uint32_t random_next(Pip *p) {
  p->rng = p->rng * 1664525U + 1013904223U;
  return p->rng;
}
static void heart(Canvas *c, int x, int y) {
  canvas_draw_box(c, x, y, 2, 2);
  canvas_draw_box(c, x + 3, y, 2, 2);
  canvas_draw_box(c, x + 1, y + 2, 3, 2);
  canvas_draw_dot(c, x + 2, y + 4);
}
static void action(Pip *p, Mood m) {
  p->mood = m;
  p->until = p->frame + (m == Sleep ? 36000 : m == Eat ? 100 : 50);
  if (m == Eat) {
    p->target = 101;
    p->meals++;
  } else if (m == Sleep)
    p->target = 23;
  else if (m == Pet) {
    p->target = p->x;
    p->bond++;
  } else if (m == Play)
    p->target = 77;
}
static void draw(Canvas *c, void *ctx) {
  Pip *p = ctx;
  furi_mutex_acquire(p->mutex, FuriWaitForever);
  canvas_clear(c);
  canvas_set_font(c, FontSecondary);
  canvas_draw_str(c, 2, 8, "Pip");
  if (p->bond)
    heart(c, 119, 2);
  canvas_draw_line(c, 0, 54, 127, 54);
  for (int i = 3; i < 128; i += 13)
    canvas_draw_line(c, i, 58, i + 2, 58);
  canvas_draw_frame(c, 92, 10, 22, 17);
  canvas_draw_line(c, 103, 10, 103, 26);
  canvas_draw_line(c, 92, 18, 113, 18);
  canvas_draw_box(c, 107, 12, 3, 3);
  canvas_draw_line(c, 8, 50, 32, 50);
  canvas_draw_line(c, 8, 47, 8, 54);
  canvas_draw_line(c, 32, 49, 32, 54);
  canvas_draw_frame(c, 11, 46, 8, 4);
  canvas_draw_circle(c, 77, 51, 3);
  canvas_draw_line(c, 96, 51, 109, 51);
  canvas_draw_line(c, 96, 51, 98, 54);
  canvas_draw_line(c, 109, 51, 107, 54);
  bool moving = p->x != p->target;
  bool asleep = p->mood == Sleep && !moving;
  int x = p->x, y = asleep ? 49 : 46 + ((p->frame / (moving ? 2 : 10)) % 2),
      h = asleep ? 9 : 14;
  canvas_draw_box(c, x - 8, y - h + 2, 17, h - 2);
  canvas_draw_box(c, x - 6, y - h, 13, 3);
  canvas_draw_box(c, x - 7, y - h - 5, 4, 7);
  canvas_draw_box(c, x + 4, y - h - 4, 4, 6);
  canvas_draw_box(c, x - 10, y - 5, 3, 4);
  canvas_draw_box(c, x - 6, y, 4, 2);
  canvas_draw_box(c, x + 4, y, 4, 2);
  canvas_set_color(c, ColorWhite);
  canvas_draw_box(c, x - 5, y - h + 5, 11, h - 7);
  canvas_set_color(c, ColorBlack);
  if (asleep || p->frame % 43 < 2) {
    canvas_draw_line(c, x - 4, y - h + 5, x - 2, y - h + 5);
    canvas_draw_line(c, x + 3, y - h + 5, x + 5, y - h + 5);
  } else {
    canvas_draw_box(c, x - 3, y - h + 4, 2, 3);
    canvas_draw_box(c, x + 4, y - h + 4, 2, 3);
  }
  canvas_draw_line(c, x + 1, y - h + 8, x + 2, y - h + 8);
  if (p->mood == Pet)
    heart(c, x + 10, y - h - 8);
  if (asleep)
    canvas_draw_str(c, x + 12, 27, "z");
  if (p->mood == Eat) {
    canvas_draw_box(c, 100, 48, 3, 2);
    if (!moving && p->frame % 4 < 2)
      canvas_draw_box(c, x + 9, y - 5, 3, 2);
  }
  if (p->mood == Play && !moving)
    canvas_draw_circle(c, 85, 44 - (p->frame % 6), 3);
  furi_mutex_release(p->mutex);
}
static void screen_commit(uint8_t *data, size_t size,
                          CanvasOrientation orientation, void *ctx) {
  UNUSED(orientation);
  Pip *p = ctx;
  if (size == sizeof(p->screen) &&
      furi_mutex_acquire(p->mutex, 0) == FuriStatusOk) {
    memcpy(p->screen, data, size);
    furi_mutex_release(p->mutex);
  }
}
static void input(InputEvent *event, void *ctx) {
  Pip *p = ctx;
  furi_message_queue_put(p->inputs, event, 0);
}
static void command(PipCliStream *cli, FuriString *args, void *ctx) {
  UNUSED(cli);
  Pip *p = ctx;
  const char *s = furi_string_get_cstr(args);
  furi_mutex_acquire(p->mutex, FuriWaitForever);
  if (strcmp(s, "screenshot") == 0) {
    uint8_t screen[1024];
    memcpy(screen, p->screen, sizeof(screen));
    furi_mutex_release(p->mutex);
    printf("PIP_SCREEN ");
    for (size_t i = 0; i < sizeof(screen); i++)
      printf("%02x", screen[i]);
    printf("\r\n");
    return;
  }
  if (strcmp(s, "feed") == 0)
    action(p, Eat);
  else if (strcmp(s, "pet") == 0)
    action(p, Pet);
  else if (strcmp(s, "sleep") == 0)
    action(p, Sleep);
  else if (strcmp(s, "wake") == 0) {
    p->mood = Idle;
    p->target = 64;
  }
  uint32_t meals = p->meals, bond = p->bond;
  Mood mood = p->mood;
  furi_mutex_release(p->mutex);
  printf("PIP meals=%lu bond=%lu mood=%u\r\n", (unsigned long)meals,
         (unsigned long)bond, (unsigned)mood);
}
static void persist(Pip *p, bool load) {
  Storage *s = furi_record_open(RECORD_STORAGE);
  storage_common_mkdir(s, EXT_PATH("apps_data/gitgotchi"));
  File *f = storage_file_alloc(s);
  Save state = {0x50495031, p->bond, p->meals};
  if (storage_file_open(f, SAVE_PATH, load ? FSAM_READ : FSAM_WRITE,
                        load ? FSOM_OPEN_EXISTING : FSOM_CREATE_ALWAYS)) {
    if (load) {
      if (storage_file_read(f, &state, sizeof(state)) == sizeof(state) &&
          state.magic == 0x50495031) {
        p->bond = state.bond;
        p->meals = state.meals;
      }
    } else
      storage_file_write(f, &state, sizeof(state));
    storage_file_close(f);
  }
  storage_file_free(f);
  furi_record_close(RECORD_STORAGE);
}
int32_t gitgotchi_app(void *arg) {
  UNUSED(arg);
  Pip *p = calloc(1, sizeof(Pip));
  p->x = 64;
  p->target = 64;
  p->rng = furi_get_tick();
  p->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
  p->inputs = furi_message_queue_alloc(8, sizeof(InputEvent));
  persist(p, true);
  ViewPort *vp = view_port_alloc();
  view_port_draw_callback_set(vp, draw, p);
  view_port_input_callback_set(vp, input, p);
  Gui *gui = furi_record_open(RECORD_GUI);
  gui_add_framebuffer_callback(gui, screen_commit, p);
  gui_add_view_port(gui, vp, GuiLayerFullscreen);
  PipCli *cli = furi_record_open(RECORD_CLI);
  pip_cli_add(cli, "gitgotchi", CliCommandFlagParallelSafe, command, p);
  bool running = true;
  while (running) {
    InputEvent event;
    FuriStatus status = furi_message_queue_get(p->inputs, &event, 100);
    furi_mutex_acquire(p->mutex, FuriWaitForever);
    if (status == FuriStatusOk && event.type == InputTypeShort) {
      switch (event.key) {
      case InputKeyBack:
        running = false;
        break;
      case InputKeyOk:
        action(p, Pet);
        break;
      case InputKeyUp:
        action(p, Sleep);
        break;
      case InputKeyDown:
        action(p, Play);
        break;
      case InputKeyLeft:
        p->target = 30;
        p->mood = Walk;
        break;
      case InputKeyRight:
        p->target = 96;
        p->mood = Walk;
        break;
      default:
        break;
      }
    }
    p->frame++;
    if (p->mood != Sleep && p->frame > p->until)
      p->mood = Idle;
    if (p->x < p->target) {
      p->x += 2;
      if (p->x > p->target)
        p->x = p->target;
    } else if (p->x > p->target) {
      p->x -= 2;
      if (p->x < p->target)
        p->x = p->target;
    }
    if (p->mood == Idle && p->frame % 60 == 0)
      p->target = 30 + random_next(p) % 62;
    furi_mutex_release(p->mutex);
    view_port_update(vp);
  }
  pip_cli_delete(cli, "gitgotchi");
  furi_record_close(RECORD_CLI);
  gui_remove_framebuffer_callback(gui, screen_commit, p);
  view_port_enabled_set(vp, false);
  gui_remove_view_port(gui, vp);
  view_port_free(vp);
  furi_record_close(RECORD_GUI);
  persist(p, false);
  furi_message_queue_free(p->inputs);
  furi_mutex_free(p->mutex);
  free(p);
  return 0;
}
