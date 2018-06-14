#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdbool.h>

// #define DEBUG 1

#ifdef DEBUG
  #define LOG(args...) printf(args)
# else
  #define LOG(args...)
#endif

#define STR_LEN 4
#define PAGE_SIZE 255
#define INITIAL_STATE 0
#define BLANK "_"

/**
  * TYPE DEFINITIONS
  */

typedef struct page page_t;
typedef struct brach branch_t;
typedef struct tr_output tr_output_t;
typedef struct tr_input tr_input_t;
typedef struct state_temp state_temp_t;
typedef struct state state_t;
typedef struct turing_machine tm_t;

/* Structure for general turing machine information */
struct turing_machine {
  unsigned int max_state; /* Highest state number */
  unsigned int max_steps; /* Maximum steps per-branch */
  state_t * states; /* [0...max_state] vector */
};

/* Structure for state information */
struct state {
  bool is_final;
  bool is_reachable; /* Does the state appear in the right part of any tr? */
  unsigned int tr_inputs_count;
  tr_input_t * tr_inputs; /* [0...tr_inputs_count] vector of
                              <input,tr_output> entries */
};

/* Temporary structure for state information during input file loading */
struct state_temp {
  unsigned int number; /* The state number */
  unsigned char tr_inputs_count;
  tr_output_t ** tr_inputs; /* [0...UCHAR_MAX] vector of
                              <tr_output> list heads , indexed by char */
  /* Links to previous and next states in the list */
  state_temp_t *prev, *next;
};

/* Structure for trainsition input->output linking */
struct tr_input {
  char input; /* Input char */
  tr_output_t * transitions; /* Linked list of transition right parts
                                <state,output,move> */
};

/* Structure for transition's output (right part) */
struct tr_output {
  unsigned int state; /* Next state */
  char output;
  char move; /* Can either be L, S, R */
  tr_output_t * next; /* Link to next transition */
};

/* Structure for computation branches */
struct branch {
  branch_t * parent; /* Pointer to parent branch */
  int state; /* Current state */
  page_t * c_page; /* Current page */
  int c_pos; /* Position on current page */
  int steps; /* Number of transitions from the root of the tree */

  /* Keep track of the extreme points of the tape for garbage collection */
  page_t *first_page, *last_page;
  int first_pos, last_pos;
};

/* Structure for memory page */
struct page {
  bool dirty; /* Does the page belong to the current branch? */
  page_t *prev, *next; /* Linked list */
  char mem[PAGE_SIZE]; /* Actual memory */
};

/* FUNCTION PROTOTYPES */
tm_t tm_create();
state_temp_t * state_temp_get(
  state_temp_t ** state_list,
  state_temp_t * c_state,
  int q
);
state_temp_t * state_temp_create(
  int q,
  state_temp_t * prev,
  state_temp_t * next
);
void state_temp_add_transition(
  state_temp_t * state,
  char input,
  int q_out,
  char output,
  char move
);
void load_transitions(tm_t * tm);

/**
  * MAIN
  */
int main() {
  int read;
  /* Load machine configuration */
  /* 1. Create turing machine instance */
  tm_t tm = tm_create();
  /* 2. Load transitions */
  char s[STR_LEN];
  read = scanf("%s", s); /* Read the "tr" string */
  getchar(); /* Flush endline */
  load_transitions(&tm);
  return 0;
}

/* Creates an initialised turing machine instance */
tm_t tm_create() {
  tm_t tm;
  tm.max_state = 0;
  tm.max_steps = 0;
  tm.states = NULL;
  return tm;
}

/** Loads the transitions from stdin:
  *  - first it puts them in a temporary structure: states are sorted in
  *    a double-linked list. Each state hold a 256-cell array which stores
  *    the output transitions for each input char.
  *  - determines the number of states and the number of different
  *    input characters for each state
  *  - then allocates an array for the states, indexed by state number.
  *    For each state it allocates an array of tr_input structs, sorted by
  *    input char, which link to their output transitions
  *  The expected transition format is:
  *  "(state) (input) (output) (move) (next state)"
  */
void load_transitions(tm_t* tm) {
  state_temp_t *state_list = NULL, *c_state = NULL;
  state_t * s;
  char temp, input, output, move;
  int q_in, q_out, read;

  temp = getchar(); /* Read first char to check wether the tr section is finished */
  ungetc(temp, stdin);  /* Then put it back in order not to alter input */
  while (temp != 'a') { /* Loading stops when the "acc" keyword is found */
    /* Scan the whole string */
    read = scanf("%d %c %c %c %d", &q_in, &input, &output, &move, &q_out);
    getchar(); /* Flush newline */
    /* Find or create the state in the list */
    c_state = state_temp_get(&state_list, c_state, q_in);
    if (c_state == NULL) {
      LOG("ERROR: Could not find or create state %d\n", q_in);
    }
    /* Append the new transition */
    state_temp_add_transition(c_state, input, q_out, output, move);
    /* Update max state number */
    if (q_in > tm->max_state) {
      tm->max_state = q_in;
    }
    if (q_out > tm->max_state) {
      tm->max_state = q_out;
    }
    /* Prepare for next iteration */
    temp = getchar(); /* Read first char to check wether the tr section is finished */
    ungetc(temp, stdin);  /* Then put it back in order not to alter input */
  }
  #ifdef DEBUG
    /* Log the state list */
    LOG("INFO: States list\n");
    c_state = state_list;
    while (c_state != NULL) {
      LOG("%d->", c_state->number);
      c_state = c_state->next;
    }
    LOG("\n");
  #endif

  /* Now create the definitive structure */
  /* First allocate the states array */
  tm->states = malloc((tm->max_state + 1) * sizeof(state_t));
  /* Initialise it */
  for (int i = 0; i <= tm->max_state; i++) {
    s = &tm->states[i];
    s->is_final = false;
    s->is_reachable = i == INITIAL_STATE;
    s->tr_inputs_count = 0;
    s->tr_inputs = NULL;
  }

  /* Now scan the states list and transfer the information */
  c_state = state_list;
  while (c_state != NULL) {
    #ifdef DEBUG
      if (c_state->number > tm->max_state) {
        LOG("ERROR: State %d does not exist", c_state->number);
      }
    #endif
    /* Create handle to the state */
    s = &tm->states[c_state->number];
    /* Initialise the tr_inputs array */
    s->tr_inputs_count = c_state->tr_inputs_count;
    s->tr_inputs = malloc(s->tr_inputs_count * sizeof(tr_input_t));
    /* Now add the new transitions */
    int c = 0; /* Current position in the array */

    /* Scan the tr_inputs array in c_state and copy the
        tr_output list from non-NULL cells */
    for (int i = 0; i <= UCHAR_MAX; i++) {
      if (c_state->tr_inputs[i] != NULL) {
        LOG("INFO: Loading transitions for %c, index %d\n", (char) i, i);
        s->tr_inputs[c].input = (char) i;
        s->tr_inputs[c].transitions = c_state->tr_inputs[i];
        c++;
      }
    }
    LOG("INFO: tr_input for state %d: %d/%d\n",
          c_state->number, c, s->tr_inputs_count);
    c_state = c_state->next;
  }

  /* Clean up temporary structure */
  while (state_list != NULL) {
    c_state = state_list;
    state_list = state_list->next;

    free(c_state->tr_inputs);
    free(c_state);
  }
  return;
}

/** Finds or adds a state to the temporary state list, keeping it ordered by insertion.
  *  Since state numbers are likely to be quite-ordered in the input file,
  *  this is more efficient than using trees in most common cases
  */
state_temp_t * state_temp_get(
  state_temp_t ** state_list,
  state_temp_t * c_state,
  int q
) {
  /* First look for the state */
  if(c_state == NULL) {
    /* The list is empty, so create the state and append it */
    c_state = state_temp_create(q, NULL, NULL);
    *state_list = c_state;
  } else if(c_state->number < q) {
    /* Scan the list right */
    while (c_state->next != NULL && c_state->next->number < q) {
      c_state = c_state->next;
    }
    /* Next state is >=q */
    if (c_state->next != NULL && c_state->next->number == q) {
      c_state = c_state->next; /* Found the state */
    } else {
      /* Insert after c_state */
      c_state->next = state_temp_create(q, c_state, c_state->next);
      c_state = c_state->next;
    }
  } else if(c_state->number > q){
    /* Scan the list left */
    while (c_state->prev != NULL && c_state->prev->number > q) {
      c_state = c_state->prev;
    }
    /* Next state is <=q */
    if (c_state->prev != NULL && c_state->prev->number == q) {
      c_state = c_state->prev; /* Found the state */
    } else {
      /* Insert before c_state */
      c_state->prev = state_temp_create(q, c_state->prev, c_state);
      c_state = c_state->prev;

      /* Update head of list */
      if (c_state->prev == NULL) {
        *state_list = c_state;
      }
    } /* If we get here, c_state->number == q */
  }
  return c_state;
}

/* Creates and returns a state_temp instance */
state_temp_t * state_temp_create(
  int q,
  state_temp_t * prev,
  state_temp_t * next
) {
  LOG("INFO: Creating state %d\n", q);
  state_temp_t * s;
  s = malloc(sizeof(state_temp_t));
  s->number = q;
  s->tr_inputs_count = 0;
  /* Create transitions array and initialise it to NULL */
  s->tr_inputs = malloc((UCHAR_MAX+1)*sizeof(tr_output_t *));
  for (int i = 0; i <= UCHAR_MAX; i++) {
    s->tr_inputs[i] = NULL;
  }
  /* Link list elements */
  s->prev = prev;
  s->next = next;
  return s;
}

/* Adds transition to an existing state during input parsing */
void state_temp_add_transition(
  state_temp_t * state,
  char input,
  int q_out,
  char output,
  char move
) {
  /* First create the transition */
  tr_output_t * tr = malloc(sizeof(tr_output_t));
  tr->state = q_out;
  tr->output = output;
  tr->move = move;
  tr->next = NULL;
  #ifdef DEBUG
    if (tr->move != 'L' && tr->move != 'S' && tr->move != 'R') {
      LOG("ERROR: Move %c @ %d:%c is invalid!\n", tr->move, state->number, input);
    }
  #endif

  /* Update input_chars_count */
  if (state->tr_inputs[(unsigned char)input] == NULL) {
    state->tr_inputs_count++;
  }

  /* Then insert it in the transitions array, at the head of the list */
  tr->next = state->tr_inputs[(unsigned char)input];
  state->tr_inputs[(unsigned char)input] = tr;

  LOG("INFO: Creating new transition\n%d,%c -> %d,%c,%c\nchars_count: %d\n",
    state->number, input, tr->state, tr->output, tr->move,
    state->tr_inputs_count);
  return;
}
