/***************************************************************************************
* Copyright (c) 2014-2022 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>
#include <cpu/cpu.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "sdb.h"

static int is_batch_mode = false;

void init_regex();
void init_wp_pool();

/* We use the `readline' library to provide more flexibility to read from stdin. */
static char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}


static int cmd_q(char *args) {
  nemu_state.state = NEMU_QUIT;
  return -1;
}

static int cmd_help(char *args){
  printf("help: Display information about all supported commands\n");
  printf("c: Continue the execution of the program\n");
  printf("q: Exit NEMU\n");
  printf("si: step N commands\n");
  printf("info: print the information of registers\n");
  printf("x: scan memory\n");
  printf("p: print the value of expression\n");
  printf("w: set watchpoint\n");
  printf("d: delete watchpoint\n");
  return 0;
};

static int cmd_si(char *args) {
  char *arg = strtok(NULL, " ");
  int n = 1;
  if (arg != NULL) {
    sscanf(arg, "%d", &n);
  }
  cpu_exec(n);
  return 0;
}

static int cmd_info(char *args) {
  char *arg = strtok(NULL, " ");
  if (strcmp(arg, "w") == 0) {
    printf("info r: print the information of registers\n");
    return 0;
  }
  if (strcmp(arg, "r") == 0) {
    isa_reg_display();
  }
  return 0;
}

static int cmd_x(char *args) {
  char *arg1 = strtok(NULL, " ");
  char *arg2 = strtok(NULL, " ");
  if (arg1 == NULL || arg2 == NULL) {
    printf("x N EXPR: scan memory\n");
    return 0;
  }
  int n;
  vaddr_t addr;
  sscanf(arg1, "%d", &n);
  sscanf(arg2, "%x", &addr);
  for (int i = 0; i < n; i++) {
    printf("0x%08x: ", addr);
    for (int j = 0; j < 4; j++) {
      printf("0x%02x ", vaddr_read(addr, 1));
      addr++;
    }
    printf("\n");
  }
  return 0;
}

static int cmd_p(char *args) {
  char *arg = strtok(NULL, " ");
  if (arg == NULL) {
    printf("p EXPR: print the value of expression\n");
    return 0;
  }
  bool success = true;
  word_t result = isa_reg_str2val(arg, &success);
  if (success) {
    printf("%d\n", result);
  } else {
    printf("Invalid expression\n");
  }
  return 0;
}

static int cmd_w(char *args) {
  char *arg = strtok(NULL, " ");
  if (arg == NULL) {
    printf("w EXPR: set watchpoint\n");
    return 0;
  }
  WP *wp = new_wp();
  strcpy(wp->expr, arg);
  bool success = true;
  wp->value = isa_reg_str2val(arg, &success);
  if (!success) {
    printf("Invalid expression\n");
    free_wp(wp);
  }
  return 0;
}

static int cmd_d(char *args) {
  char *arg = strtok(NULL, " ");
  if (arg == NULL) {
    printf("d N: delete watchpoint\n");
    return 0;
  }
  int n;
  sscanf(arg, "%d", &n);
  free_wp_by_num(n);
  return 0;
}

static struct {
  const char *name;
  const char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display information about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },
  { "si", "step N commands", cmd_si },
  { "info", "print the information of registers", cmd_info },
  { "x", "scan memory", cmd_x },
  { "p", "print the value of expression", cmd_p },
  { "w", "set watchpoint", cmd_w },
  { "d", "delete watchpoint", cmd_d },
  /* TODO: Add more commands */

};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

void sdb_set_batch_mode() {
  is_batch_mode = true;
}

void sdb_mainloop() {
  if (is_batch_mode) {
    cmd_c(NULL);
    return;
  }

  for (char *str; (str = rl_gets()) != NULL; ) {
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL) { continue; }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

#ifdef CONFIG_DEVICE
    extern void sdl_clear_event_queue();
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }
        break;
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
  }
}

void init_sdb() {
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();
}
