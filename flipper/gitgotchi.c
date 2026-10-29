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
typedef enum {
  Idle,
  Walk,
  Pet,
  Eat,
  Sleep,
  Play,
  CommitNotice,
  PushNotice,
  Celebrate
} Mood;
typedef struct {
  FuriMutex *mutex;
  FuriMessageQueue *inputs;
  int x, target;
  uint32_t frame, until, bond, meals, rng, commit_until, tick_rate;
  Mood mood;
  bool local_food;
  bool grid_open, goal_party;
  uint32_t goal_day, daily_done, daily_goal;
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
  if(m==CommitNotice || m==PushNotice)p->grid_open=false;
  p->mood = m;
  p->until = p->frame + (m == Sleep ? 36000 : m == Eat ? 100 : 50);
  if (m == Eat) {
    p->target = 101;

  } else if (m == CommitNotice) {
    p->target = p->x;
    p->until = p->frame + 60;
    p->commit_until = p->until;
  } else if (m == PushNotice) {
    p->target = p->x;
    p->meals++;
    p->until = p->frame + 20;
    if (p->until < p->commit_until + 20)
      p->until = p->commit_until + 20;
  } else if (m == Celebrate) {
    p->target = p->x;
    p->until = p->frame + 30;
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
  if (p->grid_open) {
    char label[32];
    canvas_draw_str(c, 3, 9, "COMMIT + PUSH / TODAY");
    snprintf(label, sizeof(label), "%lu / %lu completed",
             (unsigned long)p->daily_done, (unsigned long)p->daily_goal);
    canvas_draw_str(c, 3, 22, label);
    uint32_t cells =
        p->daily_done > p->daily_goal ? p->daily_done : p->daily_goal;
    if (cells > 24)
      cells = 24;
    for (uint32_t i = 0; i < cells; i++) {
      int x = 4 + (i % 12) * 10, y = 29 + (i / 12) * 10;
      canvas_draw_rframe(c, x, y, 8, 8, 1);
      if (i < p->daily_done)
        canvas_draw_box(c, x + 2, y + 2, 4, 4);
    }
    canvas_draw_str(c, 3, 61,
                    p->daily_done >= p->daily_goal ? "GOAL DONE!  Back: pet"
                                                   : "One cycle = one meal");
    furi_mutex_release(p->mutex);
    return;
  }
  const char *banner = NULL;
  if (p->mood == CommitNotice ||
      (p->mood == PushNotice && p->frame < p->commit_until))
    banner = "COMMIT DETECTED";
  else if (p->mood == PushNotice)
    banner = p->local_food ? "COMMIT REWARDED" : "PUSH CONFIRMED";
  else if (p->mood == Eat)
    banner = "SNACK DELIVERY!";
  else if (p->mood == Celebrate)
    banner = p->goal_party ? "DAILY GOAL DONE!" : "THANK YOU!";
  if (banner) {
    canvas_draw_box(c, 0, 0, 128, 12);
    canvas_set_color(c, ColorWhite);
    canvas_draw_str_aligned(c, 64, 9, AlignCenter, AlignBottom, banner);
    canvas_set_color(c, ColorBlack);
  } else {
    canvas_draw_str(c, 3, 9, "Pip");
    char progress[24];
    snprintf(progress, sizeof(progress), "%lu/%lu",
             (unsigned long)p->daily_done, (unsigned long)p->daily_goal);
    canvas_draw_str(c, 38, 9, progress);
    for (uint32_t i = 0; i < p->daily_goal && i < 6; i++) {
      canvas_draw_frame(c, 75 + i * 8, 3, 6, 6);
      if (i < p->daily_done)
        canvas_draw_box(c, 77 + i * 8, 5, 2, 2);
    }
  }
  // Window frame, moon, sill and little curtains.
  canvas_draw_rframe(c, 89, 16, 29, 20, 2);
  canvas_draw_frame(c, 92, 18, 23, 15);
  canvas_draw_line(c, 103, 18, 103, 32);
  canvas_draw_line(c, 92, 25, 114, 25);
  canvas_draw_disc(c, 110, 21, 2);
  canvas_set_color(c, ColorWhite);
  canvas_draw_disc(c, 111, 20, 2);
  canvas_set_color(c, ColorBlack);
  canvas_draw_line(c, 87, 36, 120, 36);
  for (int i = 0; i < 3; i++)
    canvas_draw_line(c, 89 + i * 2, 16, 89 + i * 2, 29 - i * 2);
  // A leafy plant and a cushioned bed.
  canvas_draw_line(c, 6, 35, 6, 44);
  canvas_draw_line(c, 6, 39, 2, 36);
  canvas_draw_line(c, 6, 37, 10, 33);
  canvas_draw_box(c, 1, 33, 3, 3);
  canvas_draw_box(c, 9, 30, 3, 4);
  canvas_draw_frame(c, 2, 43, 9, 7);
  canvas_draw_line(c, 3, 50, 9, 50);
  canvas_draw_rframe(c, 13, 46, 25, 7, 2);
  canvas_draw_rframe(c, 15, 43, 10, 5, 2);
  canvas_draw_line(c, 13, 52, 13, 55);
  canvas_draw_line(c, 37, 52, 37, 55);
  canvas_draw_line(c, 14, 49, 36, 49);
  // Wooden floor and patterned rug.
  canvas_draw_line(c, 0, 55, 127, 55);
  canvas_draw_line(c, 0, 62, 127, 62);
  for (int i = 8; i < 128; i += 26)
    canvas_draw_line(c, i, 56, i, 61);
  canvas_draw_line(c, 44, 59, 85, 59);
  canvas_draw_line(c, 47, 60, 82, 60);
  canvas_draw_circle(c, 76, 52, 3);
  canvas_draw_line(c, 75, 50, 77, 53);
  canvas_draw_rframe(c, 99, 49, 16, 5, 2);
  canvas_draw_line(c, 101, 54, 113, 54);
  heart(c, 105, 49);
  bool moving = p->x != p->target;
  bool asleep = p->mood == Sleep && !moving;
  bool chewing = p->mood == Eat && !moving;
  int x = p->x, y = 52 - ((p->frame / (moving ? 2 : 12)) % 2);
  if (p->mood == Celebrate)
    y -= (p->frame % 8 < 4 ? 4 : 0);
  if (asleep) {
    // Curled body, folded ears and a tail tucked around the paws.
    canvas_draw_rbox(c, x - 10, 40, 22, 11, 4);
    canvas_set_color(c, ColorWhite);
    canvas_draw_rbox(c, x - 7, 42, 16, 7, 3);
    canvas_set_color(c, ColorBlack);
    for (int i = 0; i < 6; i++)
      canvas_draw_box(c, x - 6 - i / 2, 37 + i, 1 + i, 1);
    canvas_draw_line(c, x - 4, 44, x - 1, 44);
    canvas_draw_line(c, x + 3, 44, x + 6, 44);
    canvas_draw_line(c, x + 1, 47, x + 3, 47);
    canvas_draw_str(c, x + 15, 33 - (p->frame / 5) % 3, "z");
  } else {
    // Rounded fox body, swept tail, pointed ears, light face and belly.
    canvas_draw_line(c, x - 11, y - 5, x - 16, y - 9 + (p->frame / 4) % 2);
    canvas_draw_line(c, x - 12, y - 4, x - 17, y - 8 + (p->frame / 4) % 2);
    canvas_draw_rbox(c, x - 10, y - 21, 22, 20, 5);
    for (int i = 0; i < 9; i++)
      canvas_draw_box(c, x - 9, y - 28 + i, 1 + i, 1);
    for (int i = 0; i < 9; i++)
      canvas_draw_box(c, x + 11 - i, y - 28 + i, 1 + i, 1);
    canvas_set_color(c, ColorWhite);
    canvas_draw_line(c, x - 7, y - 23, x - 6, y - 20);
    canvas_draw_line(c, x + 8, y - 23, x + 7, y - 20);
    canvas_draw_rbox(c, x - 7, y - 18, 16, 10, 3);
    canvas_draw_rbox(c, x - 5, y - 7, 11, 5, 2);
    canvas_set_color(c, ColorBlack);
    bool blink = p->frame % 47 < 2;
    if (blink || p->mood == Pet || p->mood == Celebrate) {
      canvas_draw_line(c, x - 5, y - 14, x - 3, y - 15);
      canvas_draw_line(c, x - 3, y - 15, x - 1, y - 14);
      canvas_draw_line(c, x + 3, y - 14, x + 5, y - 15);
      canvas_draw_line(c, x + 5, y - 15, x + 7, y - 14);
    } else {
      canvas_draw_box(c, x - 4, y - 16, 2, 3);
      canvas_draw_box(c, x + 4, y - 16, 2, 3);
      canvas_set_color(c, ColorWhite);
      canvas_draw_dot(c, x - 4, y - 16);
      canvas_draw_dot(c, x + 4, y - 16);
      canvas_set_color(c, ColorBlack);
    }
    canvas_draw_line(c, x, y - 12, x + 2, y - 12);
    canvas_draw_dot(c, x + 1, y - 11);
    if (chewing)
      canvas_draw_frame(c, x, y - 10, 3, (p->frame % 4 < 2) ? 2 : 1);
    else {
      canvas_draw_line(c, x - 1, y - 10, x + 1, y - 9);
      canvas_draw_line(c, x + 1, y - 9, x + 3, y - 10);
    }
    int stride = moving ? (p->frame % 4 < 2 ? 1 : -1) : 0;
    canvas_draw_box(c, x - 7, y - 2 + stride, 5, 2);
    canvas_draw_box(c, x + 4, y - 2 - stride, 5, 2);
    if (p->mood == Pet || p->mood == Celebrate) {
      heart(c, x + 15, y - 24 - (p->frame / 3) % 3);
      heart(c, x - 16, y - 30 + (p->frame / 4) % 2);
    }
    if (p->mood == CommitNotice) {
      canvas_draw_line(c, x + 15, y - 26, x + 15, y - 22);
      canvas_draw_dot(c, x + 15, y - 20);
    }
  }
  if (p->mood == PushNotice) {
    int drop = (int)(p->frame % 20);
    canvas_draw_frame(c, 103, 15 + drop, 7, 5);
    canvas_draw_line(c, 106, 15 + drop, 106, 19 + drop);
  }
  if (p->mood == Eat) {
    canvas_draw_box(c, 101, 47, 3, 2);
    canvas_draw_box(c, 109, 47, 3, 2);
    if (chewing) {
      canvas_draw_dot(c, x + 12, 45 + (p->frame % 3));
      canvas_draw_dot(c, x + 14, 48);
    }
  }
  if (p->mood == Play && !moving)
    canvas_draw_circle(c, 86, 46 - (p->frame % 6), 3);
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
  if (strncmp(s, "progress ", 9) == 0) {
    unsigned long day, done, goal;
    if (sscanf(s + 9, "%lu %lu %lu", &day, &done, &goal) == 3 && goal >= 1 &&
        goal <= 12) {
      p->goal_party =
          done >= goal && (p->goal_day != day || p->daily_done < goal);
      p->goal_day = day;
      p->daily_done = done;
      p->daily_goal = goal;
    }
  }
  if (strcmp(s, "grid") == 0)
    p->grid_open = !p->grid_open;
  if (strncmp(s, "rate ", 5) == 0) {
    uint32_t rate = strtoul(s + 5, NULL, 10);
    if (rate >= 300 && rate <= 2000) {
      p->tick_rate = rate;
      p->frame = (uint64_t)furi_get_tick() * 10 / p->tick_rate;
      p->until = p->frame;
      p->commit_until = 0;
      p->mood = Idle;
    }
  }
  if (strcmp(s, "feed") == 0 || strcmp(s, "feedlocal") == 0) {
    p->local_food = strcmp(s, "feedlocal") == 0;
    action(p, PushNotice);
  } else if (strcmp(s, "commit") == 0)
    action(p, CommitNotice);
  else if (strcmp(s, "pet") == 0)
    action(p, Pet);
  else if (strcmp(s, "sleep") == 0)
    action(p, Sleep);
  else if (strcmp(s, "wake") == 0) {
    p->mood = Idle;
    p->target = 64;
  }
  uint32_t meals = p->meals, bond = p->bond;
  uint32_t frame = p->frame, until = p->until;
  uint32_t day = p->goal_day, done = p->daily_done, goal = p->daily_goal;
  Mood mood = p->mood;
  furi_mutex_release(p->mutex);
  printf("PIP meals=%lu bond=%lu mood=%u\r\n", (unsigned long)meals,
         (unsigned long)bond, (unsigned)mood);
  printf("PIP_PROGRESS day=%lu done=%lu goal=%lu\r\n", (unsigned long)day,
         (unsigned long)done, (unsigned long)goal);
  printf("PIP_CLOCK frame=%lu until=%lu tick=%lu\r\n", (unsigned long)frame,
         (unsigned long)until, (unsigned long)furi_get_tick());
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
  uint32_t progress[3] = {p->goal_day, p->daily_done, p->daily_goal};
  if (storage_file_open(f, EXT_PATH("apps_data/gitgotchi/progress.dat"),
                        load ? FSAM_READ : FSAM_WRITE,
                        load ? FSOM_OPEN_EXISTING : FSOM_CREATE_ALWAYS)) {
    if (load) {
      if (storage_file_read(f, progress, sizeof(progress)) ==
              sizeof(progress) &&
          progress[2] >= 1 && progress[2] <= 12) {
        p->goal_day = progress[0];
        p->daily_done = progress[1];
        p->daily_goal = progress[2];
      }
    } else
      storage_file_write(f, progress, sizeof(progress));
    storage_file_close(f);
  }
  storage_file_free(f);
  furi_record_close(RECORD_STORAGE);
}
int32_t gitgotchi_app(void *arg) {
  UNUSED(arg);
  Pip *p = calloc(1, sizeof(Pip));
  p->tick_rate = furi_ms_to_ticks(1000);
  p->frame = (uint64_t)furi_get_tick() * 10 / p->tick_rate;
  p->daily_goal = 3;
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
    FuriStatus status =
        furi_message_queue_get(p->inputs, &event, p->tick_rate / 10);
    furi_mutex_acquire(p->mutex, FuriWaitForever);
    if (status == FuriStatusOk && event.type == InputTypeLong &&
        event.key == InputKeyRight)
      p->grid_open = !p->grid_open;
    if (status == FuriStatusOk && event.type == InputTypeShort) {
      if (p->grid_open) {
        if (event.key == InputKeyBack)
          p->grid_open = false;
      } else {
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
    }
    p->frame = (uint64_t)furi_get_tick() * 10 / p->tick_rate;
    if (p->mood != Sleep && p->frame > p->until) {
      if (p->mood == PushNotice)
        action(p, Eat);
      else if (p->mood == Eat)
        action(p, Celebrate);
      else
        p->mood = Idle;
    }
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
