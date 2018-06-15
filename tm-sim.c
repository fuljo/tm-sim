#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdbool.h>

#ifdef DEBUG
  #define LOG(args...) printf(args)
# else
  #define LOG(args...)
#endif

#define STR_LEN 4
#define PAGE_SIZE 256
#define MAX_SIZE_LINEAR_SEARCH 4
#define INITIAL_STATE 0
#define BLANK '_'

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
  unsigned int max_state; /* Highest state number */
  unsigned int max_steps; /* Maximum steps per-branch */
  rq_t * rq; /* Top of runqueue */
  state_t * states; /* [0...max_state] vector */
};

/* Structure for state information */
struct state {
  bool is_acc;
  bool is_reachable; /* Does the state appear in the right part of any tr? */
  unsigned int tr_inputs_count;
  tr_input_t * tr_inputs; /* [0...tr_inputs_count] vector of
                              <input,tr_output> entries */
};

/* Temporary structure for state information during input file loading */
struct state_temp {
  unsigned int number; /* The state number */
  unsigned char tr_inputs_count;
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
  unsigned int state; /* Next state */
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
  int first_pos, last_pos;
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

branch_t * branch_clone(branch_t * parent);
void branch_memcpy(branch_t * branch, branch_t * parent);
void branch_destroy(branch_t * branch);

char head_read(branch_t * b);
void head_write (branch_t * b, char c);
void head_move(branch_t * b, char c);

rq_t * rq_push(rq_t * rq, branch_t * b);
rq_t * rq_pop(rq_t * rq);

void load_transitions(tm_t * tm);
void load_acc(tm_t * tm);
void load_string(branch_t * b);

state_t * tm_step(tm_t * tm, branch_t * b);
char tm_run(tm_t * tm);

tr_output_t * search_tr_out(tr_input_t * v, unsigned int size, char key);

/**
  * MAIN
  */
int main() {
  int reads;
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

  /* SIMULATE ON INPUT */

  /* CLEAN MEMORY */
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
  for (unsigned int i = 0; i <= tm->max_state; i++) {
    s = &tm->states[i];

    /* Delete the output transition list for each input */
    for (unsigned int j = 0; j < s->tr_inputs_count; j++) {
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
  for (unsigned int i = 0; i <= tm->max_state; i++) {
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
    LOG("DEBUG: tr_input for state %d: %d/%d\n",
          c_state->number, c, s->tr_inputs_count);
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

  LOG("DEBUG: Creating new transition: %d,%c -> %d,%c,%c\tchars_count: %d\n",
    state->number, input, tr->state, tr->output, tr->move,
    state->tr_inputs_count);
  return;
}

/* Creates and initialises a memory page */
page_t * page_create(page_t * prev, page_t * next, bool private, char * mem) {
  LOG("DEBUG: Creating new page");
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
  LOG("DEBUG: Making private page");
  char * old_mem = page->mem;
  page->mem = (char *) malloc(PAGE_SIZE * sizeof(char));
  page->private = true;

  /* Copy the memory content */
  for(int i=0; i < PAGE_SIZE; i++) {
    page->mem[i] = old_mem[i];
  }
}

/* Creates a new branch from its parent, does not duplicate memory */
branch_t * branch_clone(branch_t * parent) {
  branch_t * b;

  /* Allocate structure */
  b = (branch_t *) malloc(sizeof(branch_t));

  /* Copy static variables */
  b->nd_parent = parent;
  b->state = parent->state;
  b->head_pos = parent->head_pos;
  b->steps = parent->steps;

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
      branch->first_page = p_child->prev; /* First page */
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
tr_output_t * search_tr_out(tr_input_t * v, unsigned int size, char key) {
  if (size <= MAX_SIZE_LINEAR_SEARCH) {
    /* Use linear search */
    for (unsigned int i = 0; i < size; i++) {
      if (v[i].input == key) {
        return v[i].transitions;
      }
    }
    return NULL;
  } else {
    /* Binary search */
    unsigned int mid = size/2;
    if(v[mid].input > key) {
      return search_tr_out(v, mid + 1, key); /* First half */
    } else {
      return search_tr_out(&v[mid + 1], size - 1, key); /* Second half */
    }
  }
}

/** EXECUTION STRATEGY:
  * - The first branch gets enqueued with b->tr = NULL
  *   this causes tm_step to look for the first transition(s)
  * - When tm_step is called, it performs this steps:
  *   + if b->nd_parent != NULL it copies the memory from his parent
  *   + if b->tr != NULL it executes the given transition (which is only one)
  *   + Then it looks for the next transition(s) from the acutal state:
  *     - if there are no transitions, the machine returns the current state
  *       if it is final (acc or non-acc)
  *     - if there are transition(s), the first one is set as
  *       b->tr and the branch remains in the queue;
  *       if there are other (non-deterministic) transitions,
  *       they are pushed in the cue and b->nd_father is set to take
  *       their parent's memory.
  *       If during the push process we find a transition which leads to
  *       a halt state, the function returns the halt state
  * - If tm_step returns NULL, the branch at the top of the rq is executed,
  *   if it returns the halt state, the entire runqueue is destroyed
  *   and the response is given
  */

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
  tr_next = search_tr_out(s->tr_inputs, s->tr_inputs_count, input);

  if (tr_next == NULL) { /* Machine needs to halt */
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
    b_child = branch_clone(b); /* Clone and set nd_parent */
    tm->rq = rq_push(tm->rq, b_child); /* Insert on top of runqueue */
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
    /* If there is no page and we're not writing a blank, create the first one */
    b->head_page = b->first_page = page_create(NULL, NULL, true, NULL);
  } else if (!b->head_page->private) {
    /* If the page is shared, make a private copy of the memory */
    page_make_private(b->head_page);
  }
  /* Now write the char */
  b->head_page->mem[b->head_pos] = c;
}

/* Move the head L, S, R and handle page fault */
void head_move(branch_t * b, char move) {
  /* TODO: Set up garbage collector */
  if (b->head_page != NULL) {
    if (move == 'L') {
      if (b->head_page->prev == NULL) { /* Left page fault */
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
      if (b->head_page->next == NULL) { /* Right page fault */
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

/* Adds a branch on top of the runqueue, returns top of runqueue */
rq_t * rq_push(rq_t * rq, branch_t * b) {
  rq_t * new = malloc(sizeof(rq_t));
  new->branch = b;
  new->next = rq;
  return new;
}

/* Removes an element from the top of the runqueue and returns it */
rq_t * rq_pop(rq_t * rq) {
  rq_t * rq_top = rq;
  if (rq != NULL) {
    rq = rq->next;
    free(rq_top);
  }
  return rq;
}
