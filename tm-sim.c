/** -----------------------------
  *   TURING MACHINE SIMULATOR
  * -----------------------------
  * (c) 2018 Alessandro Fulgini. All rights reserved
  */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdbool.h>

#define STR_LEN 4
#define PAGE_SIZE 256
#define MAX_SIZE_LINEAR_SEARCH 4
#define INITIAL_STATE 0
#define BLANK '_'
#define SYM_ACCEPT '1'
#define SYM_REFUSE '0'
#define SYM_UNDET 'U'

#ifdef DEBUG
  #define LOG(args...) printf(args)
  #define LOG_STATUS(tm, b) {\
    if(b->tr != NULL) {\
      printf("STATUS: %d, %c -> %d, %c, %c\n",\
        (int) (b->state - tm->states), head_read(b),\
        b->tr->state, b->tr->output, b->tr->move);\
    }\
  }
  #define LOG_TAPE(b) {\
    if(b->tr != NULL) {\
      printf("TAPE: ");\
      page_t * p = b->tape->first_page;\
      int i = 0;\
      while (p != NULL) {\
        printf("%c", p->mem[i]);\
        if (p == b->head_page && i == b->head_pos) {\
          printf("!");\
        }\
        i++;\
        if (i == PAGE_SIZE) {\
          i = 0;\
          p = p->next;\
        }\
      }\
      printf("\n");\
    }\
  }
# else
  #define LOG(args...)
  #define LOG_STATUS(tm, b)
  #define LOG_TAPE(b)
#endif

/**
  * TYPE DEFINITIONS
  */

typedef struct page page_t;
typedef struct tape tape_t;
typedef struct branch branch_t;
typedef struct tr_output tr_output_t;
typedef struct tr_input tr_input_t;
typedef struct state state_t;
typedef struct turing_machine tm_t;

/* Structure for general turing machine information */
struct turing_machine {
  int max_state; /* Highest state number */
  long int max_steps; /* Maximum steps per-branch */
  branch_t * rq; /* Top of runqueue */
  state_t * states; /* [0...max_state] vector */
};

/* Structure for state information */
struct state {
  bool is_acc;
  bool is_reachable; /* Does the state appear in the right part of any tr? */
  int tr_inputs_count;
  tr_input_t * tr_inputs; /* [0...tr_inputs_count] vector of
                              <input,tr_output> entries */
};

/* Structure for trainsition input->output linking */
struct tr_input {
  char input; /* Input char */
  tr_output_t * transitions; /* Linked list of transition right parts
                                <state,output,move> */
};

/* Structure for transition's output (right part) */
struct tr_output {
  int state; /* Next state */
  char output;
  char move; /* Can either be L, S, R */
  tr_output_t * next; /* Link to next transition */
};

/* Structure for computation branches */
struct branch {
  state_t * state; /* Current state */
  tr_output_t * tr; /* Transition to be executed by tm_step */
  page_t * head_page; /* TM Head page */
  int head_pos; /* Position on current page (0...PAGE_SIZE-1)*/
  long int steps; /* Number of transitions from the root of the tree */
  tape_t * tape;

  branch_t * next; /* Next branch in the runqueue */
};

/* Structure for memory page */
struct page {
  page_t *prev, *next; /* Linked list */
  char mem[PAGE_SIZE]; /* Actual memory of PAGE_SIZE */
};

/* Structure for the memory tape */
struct tape {
  int ref_count; /* Number of branches sharing this tape */
  page_t * first_page;
};

/* FUNCTION PROTOTYPES */
tm_t tm_create();
void tm_destroy(tm_t * tm);
void tm_insert_state(int q);

void state_insert_transition(
  state_t * state,
  char input,
  int q_out,
  char output,
  char move
);

page_t * page_create(page_t * prev, page_t * next, char * mem);

branch_t * branch_clone(branch_t * parent, tr_output_t * tr);
void tape_make_private(branch_t * branch);
void branch_destroy(branch_t * branch);

char head_read(branch_t * b);
void head_write (branch_t * b, char c);
void head_move(branch_t * b, char c);

void rq_enqueue(branch_t ** rq, branch_t * b);
branch_t * rq_dequeue(branch_t ** rq);

void load_transitions(tm_t * tm);
void load_acc(tm_t * tm);
void load_string(branch_t * b);

char tm_run(tm_t * tm);
char tm_compute_rq(tm_t * tm);
state_t * tm_step(tm_t * tm, branch_t * b);

tr_input_t * search_tr_input(tr_input_t * v, int p, int r, char key);

/**
  * MAIN
  */
int main() {
  int reads;
  char res;
  /* LOAD MACHINE CONFIGURATION */

  /* 1. Create turing machine instance */
  tm_t tm = tm_create();

  /* 2. Load transitions */
  char s[STR_LEN];
  reads = scanf("%s", s); /* Read the "tr" string */
  getchar(); /* Flush endline */
  load_transitions(&tm);

  /* 3. Load acceptance states */
  reads = scanf("%s", s); /* Read the "acc" string */
  getchar(); /* Flush endline */
  load_acc(&tm);

  /* 4. Load max steps */
  reads = scanf("%s", s); /* Read the "max" string */
  getchar(); /* Flush endline */
  reads = scanf("%ld", &tm.max_steps); /* Read max steps number */
  if (reads) getchar(); /* Flush endline */

  /* 5. Simulate on input */
  reads = scanf("%s", s); /* Read the "run" string */
  getchar(); /* Flush endline */
  while ((res = getchar()) != EOF) {
    ungetc(res, stdin);
    res = tm_run(&tm);
    printf("%c\n", res);
  }

  /* 6. Clean memory */
  tm_destroy(&tm);
  return 0;
}

/* Creates an initialised turing machine instance */
tm_t tm_create() {
  tm_t tm;
  tm.max_state = 0;
  tm.max_steps = 0;
  tm.rq = NULL;
  tm.states = (state_t *) malloc(sizeof(state_t)); /* Initial state */
  tm.states[0].tr_inputs_count = 0;
  tm.states[0].tr_inputs = NULL;
  tm.states[0].is_acc = false;
  return tm;
}

/* Destroys a turing machine and deallocates memory */
void tm_destroy(tm_t * tm) {
  state_t * s;
  tr_output_t *tr, *tr_next;
  /* Delete each state */
  for (int i = 0; i <= tm->max_state; i++) {
    s = &tm->states[i];

    /* Delete the output transition list for each input */
    for (int j = 0; j < s->tr_inputs_count; j++) {
      tr = s->tr_inputs[j].transitions;
      while (tr != NULL) {
        tr_next = tr->next;
        free(tr);
        tr = tr_next;
      }
    }

    /* Delete the input list itself */
    free(s->tr_inputs);
  }

  /* Delete the states list */
  free(tm->states);
  return;
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
  state_t * s;
  char input, output, move;
  int q_in, q_out, reads, max;
  do { /* Loading stops when the "acc" keyword is found */
    /* Scan the whole string */
    reads = scanf("%d %c %c %c %d", &q_in, &input, &output, &move, &q_out);
    if(reads) { /* reads = 0 means that the "tr" section is finished */
      getchar(); /* Flush newline */

      /* First extend the states array if necessary */
      max = q_in > q_out ? q_in : q_out;
      if (max > tm->max_state) { /* Extend the size */
        LOG("DEBUG: New status array limit: %d\n", max);
        tm->states = (state_t *)
                            realloc(tm->states, (max + 1) * sizeof(state_t));
        while (tm->max_state < max) { /* Initialise new states */
          s = &tm->states[tm->max_state + 1];
          s->tr_inputs_count = 0;
          s->tr_inputs = NULL;
          s->is_acc = false;
          tm->max_state++;
        }
      }

      /* Insert the new transition */
      state_insert_transition(&tm->states[q_in], input, q_out, output, move);

    }
  } while(reads != 0);

  LOG("INFO: Max state: %d\n", tm->max_state);
}

/* Loads acceptance states */
void load_acc(tm_t * tm) {
  int q, reads;
  LOG("INFO: Acceptance states: ");
  do {
    reads = scanf("%d", &q);
    if (reads) {
      getchar(); /* Flush endline */
      LOG("%d, ", q);
      if (q <= tm->max_state) {
        tm->states[q].is_acc = true;
      } /* If the state is not in the list it would be unreachable */
    }
  } while(reads != 0);
  LOG("\n");
  return;
}

/* Adds transition to an existing state during input parsing */
void state_insert_transition(
  state_t * s,
  char input,
  int q_out,
  char output,
  char move
) {
  tr_output_t * tr_out = NULL;
  tr_input_t * tr_in = NULL;

  /* First create the transition */
  tr_out = (tr_output_t *) malloc(sizeof(tr_output_t));
  tr_out->state = q_out;
  tr_out->output = output;
  tr_out->move = move;

  if (s->tr_inputs_count == 0) {
    /* Insert the first element */
    s->tr_inputs = (tr_input_t *) malloc(sizeof(tr_input_t));
    s->tr_inputs_count++;
    tr_in = s->tr_inputs;

    /* Then set up the new entry */
    tr_in->input = input;
    tr_in->transitions = NULL;
  } else {
    /* Look if the input transition already exists */
    tr_in = search_tr_input(s->tr_inputs, 0, s->tr_inputs_count-1, input);

    /* If it does not exist, extend the array by one */
    if (tr_in == NULL) {
      s->tr_inputs_count++;
      s->tr_inputs = (tr_input_t *)
                realloc(s->tr_inputs, s->tr_inputs_count * sizeof(tr_input_t));
      /* Determine where the new element must be inserted, while shifting
          entries above it up by one */
      tr_in = &s->tr_inputs[s->tr_inputs_count - 1]; /* Begin from the last entry */
      while (tr_in != s->tr_inputs && (tr_in-1)->input > input) {
        /* Shift the entry up */
        tr_in->input = (tr_in-1)->input;
        tr_in->transitions = (tr_in-1)->transitions;
        tr_in--;
      }

      /* Then set up the new entry */
      tr_in->input = input;
      tr_in->transitions = NULL;
    }
  }

  /* Insert the new transition at the head of the list */
  tr_out->next = tr_in->transitions;
  tr_in->transitions = tr_out;
}

/* Creates and initialises a memory page */
page_t * page_create(page_t * prev, page_t * next, char * mem) {
  LOG("DEBUG: Creating new page: %p\t%p\n", prev, next);
  page_t * p = (page_t *) malloc(sizeof(page_t));
  p->prev = prev;
  p->next = next;

  if (mem == NULL) { /* If no memory to copy is given, intialize new one */
    for (int i = 0; i < PAGE_SIZE; i++) {
      p->mem[i] = BLANK;
    }
  } else { /* Copy the memory, char by char */
    for (int i = 0; i < PAGE_SIZE; i++) {
      p->mem[i] = mem[i];
    }
  }
  return p;
}

/* Creates a new branch from its parent, does not duplicate memory */
branch_t * branch_clone(branch_t * parent, tr_output_t * tr) {
  branch_t * b;

  /* Allocate structure */
  b = (branch_t *) malloc(sizeof(branch_t));

  /* Copy static variables */
  b->state = parent->state;
  b->head_pos = parent->head_pos;
  b->steps = parent->steps;
  b->tr = tr;

  /* Share memory with parent */
  b->tape = parent->tape;
  b->tape->ref_count++;
  b->head_page = parent->head_page;

  return b;
}

/* Makes a private copy of the tape */
void tape_make_private(branch_t * branch) {
  tape_t * parent = branch->tape;
  page_t *p_parent, *p_child;

  /* Create a new tape descriptor */
  branch->tape = (tape_t *) malloc(sizeof(tape_t));
  branch->tape->ref_count = 1;
  parent->ref_count--;

  /* Copy pages */
  p_parent = parent->first_page;
  p_child = NULL;
  while (p_parent != NULL) {
    /* Create new page with shared memory and insert it at the end of the list */
    p_child = page_create(p_child, NULL, p_parent->mem);
    if (p_child->prev != NULL) {
      p_child->prev->next = p_child; /* Link next to previous */
    } else {
      branch->tape->first_page = p_child; /* First page */
    }

    /* Set the same head page */
    if (p_parent == branch->head_page) {
      branch->head_page = p_child;
    }

    p_parent = p_parent->next;
  }
}

/* Destroy branch and deallocate memory */
void branch_destroy(branch_t * branch) {
  /* De-reference the tape */
  branch->tape->ref_count--;
  if (branch->tape->ref_count == 0) {
    LOG("DEBUG: Clearing unreferenced tape\n");
    /* If the tape isn't referenced by any branch, free it */
    page_t *p, *p_next;

    /* Delete pages */
    p = branch->tape->first_page;
    while (p != NULL) {
      p_next = p->next;
      free(p);
      p = p_next;
    }

    /* Delete the tape descipror */
    free(branch->tape);
  }

  /* Delete the branch itself */
  free(branch);
}

/* Return output transition list */
tr_input_t * search_tr_input(tr_input_t * v, int p, int r, char key) {
  if (r < p || key < v[p].input || key > v[r].input) {
    return NULL;
  }
  if ((r - p) <= MAX_SIZE_LINEAR_SEARCH) {
    /* Use linear search */
    while(p <= r) {
      if (v[p].input == key) {
        return &v[p];
      }
      p++;
    }
    return NULL;
  } else {
    /* Binary search */
    int mid = p + (r - p)/2;
    if (v[mid].input == key) {
      return &v[mid];
    }
    if(v[mid].input > key) {
      return search_tr_input(v, p, mid - 1, key); /* First half */
    } else {
      return search_tr_input(v, mid + 1 , r, key); /* Second half */
    }
  }
}

/* Computes one string from stdin and returns the response 0, 1, U */
char tm_run(tm_t * tm) {
  branch_t *root, *b;
  char c;
  long int reads = 0;

  /* 1. Create the "root" branch */
  root = (branch_t *) malloc(sizeof(branch_t));
  root->head_page = NULL; /* Will cause page fault */
  root->tape = (tape_t *) malloc(sizeof(tape_t));
  root->tape->first_page = NULL;
  root->tape->ref_count = 1;
  root->head_pos = 0;
  root->steps = 0;
  root->tr = NULL;
  root->next = NULL;
  /* TODO: Is this really useful? */
  if (tm->states != NULL) { /* Set initial state */
    root->state = &tm->states[INITIAL_STATE];
  } else {
    LOG("ERROR: The machine has no states");
    return SYM_REFUSE;
  }

  /* 2. Load the input string */
  c = getchar();
  while (c != '\n' && c != EOF) {
    /* Read the string till the end or max_steps */
    if (reads <= tm->max_steps) {
      head_write(root, c);
      head_move(root, 'R');
    }
    c = getchar();
    reads++;
  }
  /* Reset head's position */
  root->head_page = root->tape->first_page;
  root->head_pos = 0;

  /* 3. Run the computation */
  rq_enqueue(&tm->rq, root);
  c = tm_compute_rq(tm);

  /* 4. Empty the runqueue */
  while(tm->rq != NULL) {
    b = rq_dequeue(&tm->rq);
    branch_destroy(b);
  }

  /* Return evaluation */
  return c;
}

/* Execute the runqueue until it's empty or a final state is reached */
/* Return code: 0: refuse, 1: accept, U: undetermined */
char tm_compute_rq(tm_t * tm) {
  branch_t * b;
  state_t * s;
  bool has_preempted = false;

  while (tm->rq != NULL) {
    b = rq_dequeue(&tm->rq); /* Branch to be executed */

    if (b->steps == tm->max_steps){ /* Check if preemption is needed */
      /* Preempt the branch */
      branch_destroy(b);
      has_preempted = true;
    } else { /* No preemption => execute transition */
      LOG_STATUS(tm, b);
      LOG_TAPE(b);
      s = tm_step(tm, b);
      if (s != NULL) { /* Machine halted in this state */
        if (s->tr_inputs_count == 0 && s->is_acc) { /* It is an acceptance state */
          LOG("INFO: Accepting...\n");
          branch_destroy(b);
          return SYM_ACCEPT;
        } else { /* The transition is undefined */
          /* This branch has terminated */
          LOG("DEBUG: Dequeuing dead branch\n");
          branch_destroy(b);
        }
      }
    }
  }

  /** If we got here, computation has finished without reaching
    * acceptance states.
    * If we preempted a branch at least once, the machine could have terminated
    * so the response must be undetermined.
    */
  return has_preempted ? SYM_UNDET : SYM_REFUSE;
}

/** Takes in a branch from the queue.
  * Then executes the given transition, if any.
  * Looks for the next transition, if it find none it returns the halt state,
  * else it returns NULL.
  * Re-enqueues the branch(es) with the next transition(s).
  */
state_t * tm_step(tm_t * tm, branch_t * b) {
  branch_t * b_child;
  state_t * s = NULL;
  tr_output_t * tr_next;
  tr_input_t * tr_in;
  char input;

  /* Execute given transition */
  if (b->tr != NULL) {
    LOG("DEBUG: Doing transition -> %d, %c, %c\n",
      b->tr->state, b->tr->output, b->tr->move);
    s = &tm->states[b->tr->state]; /* Save next state */

    /* Complete the transition */
    b->state = s;
    head_write(b, b->tr->output);
    head_move(b, b->tr->move);
    b->steps++;
  }

  /* Look for the next transition(s) */
  s = b->state; /* Save current state */
  input = head_read(b); /* Read input */
  tr_in = search_tr_input(s->tr_inputs, 0, s->tr_inputs_count - 1, input);
  tr_next = tr_in == NULL ? NULL : tr_in->transitions;

  if (tr_next == NULL) { /* Machine needs to halt */
    LOG("DEBUG: Reached halt state\n");
    return s; /* Returns the halt state */
  }

  /* Set the first transition as the next on this branch */
  b->tr = tr_next;
  rq_enqueue(&tm->rq, b);

  /** If there are other (non-deterministic) transitions, they are
    * pushed on top of the first one, so they will be executed first
    * and copy the memory of their parent at the moment of branching.
    * In this way we don't waste the parent's memory.
    */
  tr_next = tr_next->next;
  while (tr_next != NULL) {
    b_child = branch_clone(b, tr_next); /* Clone with shared memory */
    rq_enqueue(&tm->rq, b_child);
    tr_next = tr_next->next;
  }

  return NULL; /* Next branch in the runqueue will be executed */
}

/** Read the char in the cell under head, even if no page is allocated.
  * NOTE: Assuming that if the head is set, it is in a valid position
  */
char head_read(branch_t * b) {
  /* First check if any page is allocated */
  if (b->head_page == NULL) {
    return BLANK; /* Do not waste time+space allocating memory */
  } else {
    return b->head_page->mem[b->head_pos];
  }
}

/** Write given char in the cell under the head.
  * also handles page fault if there is no page allocated
  * and makes a page private if needed
  */
void head_write(branch_t * b, char c) {
  /* First check if any page is allocated */
  if (b->head_page == NULL && c != BLANK) {
    LOG("DEBUG: Page fault, creating first page\n");
    /* If there is no page and we're not writing a blank, create the first one */
    b->head_page = page_create(NULL, NULL, NULL);
    b->tape->first_page = b->head_page;
  }
  if (b->head_page->mem[b->head_pos] != c) { /* Only write if different */
    if (b->tape->ref_count > 1) { /* If the tape is shared, make it private */
      tape_make_private(b);
    }
    /* Now write the char */
    b->head_page->mem[b->head_pos] = c;
  }
}

/* Move the head L, S, R and handle page fault */
void head_move(branch_t * b, char move) {
  if (b->head_page != NULL) {
    if (move == 'L') {
      if (b->head_page->prev == NULL && b->head_pos == 0) { /* Left page fault */
        /* Create the new page */
        b->head_page->prev = page_create(NULL, b->head_page, NULL);
        b->tape->first_page = b->head_page->prev;
      }
      if (b->head_pos == 0) { /* Move to previous page */
        b->head_page = b->head_page->prev;
        b->head_pos = PAGE_SIZE - 1;
      } else { /* Just decrement the position */
        b->head_pos--;
      }
    } else if (move == 'R') {
      if (b->head_page->next == NULL && b->head_pos == PAGE_SIZE - 1) { /* Right page fault */
        /* Create the new page an mark it private */
        b->head_page->next = page_create(b->head_page, NULL, NULL);
      }
      if (b->head_pos == PAGE_SIZE - 1) { /* Move to next page */
        b->head_page = b->head_page->next;
        b->head_pos = 0;
      } else { /* Just increment the position */
        b->head_pos++;
      }
    }
  } /* If there is no page allocated, just do nothing */
}

/* Adds a branch on top of the runqueue */
void rq_enqueue(branch_t ** rq, branch_t * b) {
  b->next = *rq;
  *rq = b;
}

/* Removes a branch from the top of the runqueue and returns it */
branch_t * rq_dequeue(branch_t ** rq) {
  branch_t * b;
  if (*rq != NULL) {
    b = *rq;
    *rq = b->next;
    b->next = NULL;
    return b;
  } else {
    return NULL;
  }
}
