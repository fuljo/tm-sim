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
#define PAGE_SIZE 512
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
  #define LOG_TAPE(branch) {\
    branch_t * b = branch;\
    if(b->tr != NULL) {\
      printf("TAPE: ");\
      if (b->nd_parent != NULL) {\
        b = b->nd_parent;\
      }\
      page_t * p = b->first_page;\
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
  #define LOG_TAPE(tm)
#endif

/**
  * TYPE DEFINITIONS
  */

typedef struct page page_t;
typedef struct branch branch_t;
typedef struct runqueue rq_t;
typedef struct tr_output tr_output_t;
typedef struct tr_input tr_input_t;
typedef struct state_temp state_temp_t;
typedef struct state state_t;
typedef struct turing_machine tm_t;

/* Structure for general turing machine information */
struct turing_machine {
  int max_state; /* Highest state number */
  int max_steps; /* Maximum steps per-branch */
  rq_t * rq; /* Top of runqueue */
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

/* Temporary structure for state information during input file loading */
struct state_temp {
  int number; /* The state number */
  int tr_inputs_count;
  tr_output_t * tr_inputs[UCHAR_MAX + 1]; /* [0...UCHAR_MAX] vector of
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
  int state; /* Next state */
  char output;
  char move; /* Can either be L, S, R */
  tr_output_t * next; /* Link to next transition */
};

/* Structure for computation branches */
struct branch {
  branch_t * nd_parent; /* Pointer to parent branch if memory needs to be copied */
  state_t * state; /* Current state */
  tr_output_t * tr; /* Transition to be executed by tm_step */
  page_t * head_page; /* TM Head page */
  int head_pos; /* Position on current page (0...PAGE_SIZE-1)*/
  int steps; /* Number of transitions from the root of the tree */

  /* Keep track of the extreme points of the tape for garbage collection */
  page_t * first_page;
};

/* Structure for memory page */
struct page {
  bool private; /* Does the page belong to the current branch? */
  page_t *prev, *next; /* Linked list */
  char * mem; /* Actual memory of PAGE_SIZE */
};

/* Branch runqueue element */
struct runqueue {
  rq_t * next;
  branch_t * branch;
};

/* FUNCTION PROTOTYPES */
tm_t tm_create();
state_temp_t * state_temp_get(
  state_temp_t ** state_list,
  state_temp_t * c_state,
  int q
);
void tm_destroy(tm_t * tm);
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

page_t * page_create(page_t * prev, page_t * next, bool private, char * mem);
void page_destroy(page_t * page);
void page_make_private(page_t * page);

branch_t * branch_clone(branch_t * parent, tr_output_t * tr);
void branch_memcpy(branch_t * branch, branch_t * parent);
void branch_destroy(branch_t * branch);

char head_read(branch_t * b);
void head_write (branch_t * b, char c);
void head_move(branch_t * b, char c);

void rq_push(rq_t ** rq, branch_t * b);
branch_t * rq_pop(rq_t ** rq);

void load_transitions(tm_t * tm);
void load_acc(tm_t * tm);
void load_string(branch_t * b);

char tm_run(tm_t * tm);
char tm_compute_rq(tm_t * tm);
state_t * tm_step(tm_t * tm, branch_t * b);

tr_output_t * search_tr_out(tr_input_t * v, int p, int r, char key);

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
  reads = scanf("%d", &tm.max_steps); /* Read max steps number */
  if (reads) getchar(); /* Flush endline */

  /* 5. Simulate on input */
  reads = scanf("%s", s); /* Read the "max" string */
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
  tm.states = NULL;
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
  state_temp_t *state_list = NULL, *c_state = NULL;
  state_t * s;
  char input, output, move;
  int q_in, q_out, reads;
  do { /* Loading stops when the "acc" keyword is found */
    /* Scan the whole string */
    reads = scanf("%d %c %c %c %d", &q_in, &input, &output, &move, &q_out);
    if(reads) { /* reads = 0 means that the "tr" section is finished */
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
    }
  } while(reads != 0);
  #ifdef DEBUG
    /* Log the state list */
    LOG("INFO: States list: ");
    c_state = state_list;
    while (c_state != NULL) {
      LOG("%d->", c_state->number);
      c_state = c_state->next;
    }
    LOG("\n");
  #endif

  /* Now create the definitive structure */
  /* First allocate the states array */
  tm->states = (state_t *) malloc((tm->max_state + 1) * sizeof(state_t));
  /* Initialise it */
  for (int i = 0; i <= tm->max_state; i++) {
    s = &tm->states[i];
    s->is_acc = false;
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
    s->tr_inputs = (tr_input_t *) malloc(s->tr_inputs_count * sizeof(tr_input_t));
    /* Now add the new transitions */
    int c = 0; /* Current position in the array */

    /* Scan the tr_inputs array in c_state and copy the
        tr_output list from non-NULL cells */
    for (int i = 0; i <= UCHAR_MAX; i++) {
      if (c_state->tr_inputs[i] != NULL) {
        s->tr_inputs[c].input = (char) i;
        s->tr_inputs[c].transitions = c_state->tr_inputs[i];
        c++;
      }
    }
    c_state = c_state->next;
  }

  /* Clean up temporary structure */
  c_state = state_list;
  while (state_list != NULL) {
    state_list = state_list->next;
    free(c_state);
    c_state = state_list;
  }
  return;
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

      if (c_state->next != NULL) { /* link to next state */
        c_state->next->prev = c_state;
      }
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

      if (c_state->prev != NULL) { /* Link to previous */
        c_state->prev->next = c_state;
      } else { /* Update head of list */
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
  state_temp_t * s;
  s = (state_temp_t *) malloc(sizeof(state_temp_t));
  s->number = q;
  s->tr_inputs_count = 0;
  /* Initialise the transitions array to NULL */
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
  tr_output_t * tr = (tr_output_t *) malloc(sizeof(tr_output_t));
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

  return;
}

/* Creates and initialises a memory page */
page_t * page_create(page_t * prev, page_t * next, bool private, char * mem) {
  LOG("DEBUG: Creating new page: %p\t%p\t%d\n", prev, next, private);
  page_t * p = (page_t *) malloc(sizeof(page_t));
  p->prev = prev;
  p->next = next;
  p->private = private;
  /* If no memory to copy is given, allocate new one */
  if (mem == NULL) {
    mem = (char *) malloc(PAGE_SIZE * sizeof(char));
    /* TODO: Do not initalise memory and use garbage collector */
    for (int i = 0; i < PAGE_SIZE; i++) {
      mem[i] = BLANK;
    }
  }
  p->mem = mem;
  return p;
}

/* Clears out a page */
void page_destroy(page_t * page) {
  free(page->mem);
  free(page);
}

/* Copy the page's shared memory to a new private memory */
void page_make_private(page_t * page) {
  LOG("DEBUG: Making private page\n");
  char * old_mem = page->mem;
  page->mem = (char *) malloc(PAGE_SIZE * sizeof(char));
  page->private = true;

  /* Copy the memory content */
  for(int i=0; i < PAGE_SIZE; i++) {
    page->mem[i] = old_mem[i];
  }
}

/* Creates a new branch from its parent, does not duplicate memory */
branch_t * branch_clone(branch_t * parent, tr_output_t * tr) {
  branch_t * b;

  /* Allocate structure */
  b = (branch_t *) malloc(sizeof(branch_t));

  /* Copy static variables */
  b->nd_parent = parent;
  b->state = parent->state;
  b->head_pos = parent->head_pos;
  b->steps = parent->steps;
  b->tr = tr;

  /* Initialise memory to NULL, it will be derived from parent when branch
      is evaluated */
  b->first_page = NULL;
  b->head_page = NULL;

  return b;
}

/* Copies branch pages from parent, the memory remains shared */
void branch_memcpy(branch_t * branch, branch_t * parent) {
  page_t *p_parent, *p_child;

  if (parent == NULL) {
    /* Nothing to copy */
    return;
  }

  /* Copy memory descriptors */
  p_parent = parent->first_page;
  p_child = NULL;
  while (p_parent != NULL) {
    /* Create new page with shared memory and insert it at the end of the list */
    p_child = page_create(p_child, NULL, false, p_parent->mem);
    if (p_child->prev != NULL) {
      p_child->prev->next = p_child; /* Link next to previous */
    } else {
      branch->first_page = p_child; /* First page */
    }

    /* Set the same head page */
    if (p_parent == parent->head_page) {
      branch->head_page = p_child;
    }

    p_parent = p_parent->next;
  }
}

/* Destroy branch and deallocate memory */
void branch_destroy(branch_t * branch) {
  page_t *p, *p_next;

  /* Delete page descriptors and private mem */
  p = branch->first_page;
  while (p != NULL) {
    if (p->private) {
      free(p->mem);
    }
    p_next = p->next;
    free(p);
    p = p_next;
  }

  /* Delete the branch itself */
  free(branch);
}

/* Return output transition list */
tr_output_t * search_tr_out(tr_input_t * v, int p, int r, char key) {
  if (r < p || key < v[p].input || key > v[r].input) {
    return NULL;
  }
  if ((r - p) <= MAX_SIZE_LINEAR_SEARCH) {
    /* Use linear search */
    while(p <= r) {
      if (v[p].input == key) {
        return v[p].transitions;
      }
      p++;
    }
    return NULL;
  } else {
    /* Binary search */
    int mid = p + (r - p)/2;
    if (v[mid].input == key) {
      return v[mid].transitions;
    }
    if(v[mid].input > key) {
      return search_tr_out(v, p, mid - 1, key); /* First half */
    } else {
      return search_tr_out(v, mid + 1 , r, key); /* Second half */
    }
  }
}

/* Computes one string from stdin and returns the response 0, 1, U */
char tm_run(tm_t * tm) {
  branch_t *root, *b;
  char c;

  /* 1. Create the "root" branch */
  root = malloc(sizeof(branch_t));
  root->nd_parent = NULL;
  root->head_page = NULL; /* Will cause page fault */
  root->first_page = NULL;
  root->head_pos = 0;
  root->steps = 0;
  root->tr = NULL;
  if (tm->states != NULL) { /* Set initial state */
    root->state = &tm->states[INITIAL_STATE];
  } else {
    LOG("ERROR: The machine has no states");
    return SYM_REFUSE;
  }

  /* 2. Load the input string */
  c = getchar();
  while (c != '\n' && c != EOF) { /* Read the string till the end */
    head_write(root, c);
    head_move(root, 'R');
    c = getchar();
  }
  /* Reset head's position */
  root->head_page = root->first_page;
  root->head_pos = 0;

  /* 3. Run the computation */
  rq_push(&tm->rq, root);
  c = tm_compute_rq(tm);

  /* 4. Empty the runqueue */
  while(tm->rq != NULL) {
    b = rq_pop(&tm->rq);
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
    b = tm->rq->branch; /* Branch to be executed */

    if (b->steps == tm->max_steps){ /* Check if preemption is needed */
      /* Preempt the branch */
      rq_pop(&tm->rq);
      branch_destroy(b);
      has_preempted = true;
    } else { /* No preemption => execute transition */
      LOG_STATUS(tm, tm->rq->branch);
      LOG_TAPE(tm->rq->branch);
      s = tm_step(tm, tm->rq->branch);
      if (s != NULL) { /* Machine halted in this state */
        if (s->tr_inputs_count == 0 && s->is_acc) { /* It is an acceptance state */
          LOG("INFO: Accepting...\n");
          return SYM_ACCEPT;
        } else { /* The transition is undefined */
          /* This branch has terminated */
          LOG("DEBUG: Dequeuing dead branch\n");
          rq_pop(&tm->rq);
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

/** Takes in a branch from the queue, if needed copies memory from parent.
  * Then executes the given transition, if any.
  * Looks for the next transition, if it find none it returns the halt state,
  * else it returns NULL.
  * If it finds more than one transition (non-deterministic case) it pushes
  * them at the top of the runqueue.
  */
state_t * tm_step(tm_t * tm, branch_t * b) {
  branch_t * b_child;
  state_t * s = NULL;
  tr_output_t * tr_next;
  char input;

  /* Execute given transition */
  if (b->tr != NULL) {
    LOG("DEBUG: Doing transition -> %d, %c, %c\n",
      b->tr->state, b->tr->output, b->tr->move);
    s = &tm->states[b->tr->state]; /* Save next state */

    /* If we come from a non-deterministic transition, copy parent's memory */
    if (b->nd_parent != NULL) {
      branch_memcpy(b, b->nd_parent);
      b->nd_parent = NULL; /* Reset non-determinism */
    }

    /* Complete the transition */
    b->state = s;
    head_write(b, b->tr->output);
    head_move(b, b->tr->move);
    b->steps++;
  }

  /* Look for the next transition(s) */
  s = b->state; /* Save current state */
  input = head_read(b); /* Read input */
  tr_next = search_tr_out(s->tr_inputs, 0, s->tr_inputs_count - 1, input);

  if (tr_next == NULL) { /* Machine needs to halt */
    LOG("DEBUG: Reached halt state\n");
    return s; /* Returns the halt state */
  }

  /* Set the first transition as the next on this branch */
  b->tr = tr_next;

  /** If there are other (non-deterministic) transitions, they are
    * pushed on top of the first one, so they will be executed first
    * and copy the memory of their parent at the moment of branching.
    * In this way we don't waste the parent's memory.
    */
  tr_next = tr_next->next;
  while (tr_next != NULL) {
    b_child = branch_clone(b, tr_next); /* Clone and set nd_parent */
    rq_push(&tm->rq, b_child); /* Insert on top of runqueue */
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
    b->first_page = page_create(NULL, NULL, true, NULL);
    b->head_page = b->first_page;
  }
  if (b->head_page->mem[b->head_pos] != c) { /* Only write if different */
    if (!b->head_page->private) {
      /* If the page is shared, make a private copy of the memory */
      page_make_private(b->head_page);
    }
    /* Now write the char */
    b->head_page->mem[b->head_pos] = c;
  }
}

/* Move the head L, S, R and handle page fault */
void head_move(branch_t * b, char move) {
  /* TODO: Set up garbage collector */
  if (b->head_page != NULL) {
    if (move == 'L') {
      if (b->head_page->prev == NULL && b->head_pos == 0) { /* Left page fault */
        /* Create the new page an mark it private */
        b->head_page->prev = page_create(NULL, b->head_page, true, NULL);
        b->first_page = b->head_page->prev;
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
        b->head_page->next = page_create(b->head_page, NULL, true, NULL);
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
void rq_push(rq_t ** rq, branch_t * b) {
  rq_t * new = malloc(sizeof(rq_t));
  new->branch = b;
  new->next = *rq;
  *rq = new;
}

/* Removes a branch from the top of the runqueue and returns it */
branch_t * rq_pop(rq_t ** rq) {
  rq_t * elem;
  branch_t * b;
  if (*rq != NULL) {
    elem = *rq;
    b = elem->branch;
    *rq = (*rq)->next;
    free(elem);
    return b;
  } else {
    return NULL;
  }
}
