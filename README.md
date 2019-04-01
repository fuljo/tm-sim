# Turing Machine simulator

*This project has been developed as part of the "Algoritmi e Principi dell'Informatica" course at [Politecnico di Milano](https://www.polimi.it).*

Yet another C program that simulates a single-strip nondeterministic
Turing Machine.

## Input format

The program expects its input from stdio with the following format.

#### Transitions

The transitions block starts with the `tr` keyword and a list of
state transitions follow (one per line).
Each transition has the following format:

`<in_state> <in_char> <out_char> <move> <next_state>`
where:
+ states are represented by integers
+ chars are ASCII characters, `_` representing the special *blank* charachter
+ move can be any of `L`, `S` or `R` for left, stop or right

#### Accepting states

The accepting states block starts with the `acc` keyword.
Accepting states are written one per line.

#### Maximum steps

The number written under `max` sets the maximum number of steps
the machine can perform on each branch of the computation tree
before giving up.

#### Input strings

The `run` section contains the strings on which to simulate the
machine, one per line.

*__Example of the input stream:__*

```
tr
1 a a R 1
2 _ _ R 0
0 _ _ L 3
3 b b L 3
3 a b L 4
acc
4
max
5000
run
ba
ab
aaaabb
bbaa
aaaabbbbbb
```

## Output format

The output is written to stdout.

Each line contains exactly a symbol between
+ `0`: string not accepted
+ `1`: string accepted
+ `U`: undetermined, reached max steps.

*__Example of the output stream:__*
```
0
1
U
0
U
```

## Compiling

Just run `make`

## Debugging

The program can be compiled with the `-DDEBUG` flag to turn on debug
prints on stdout.
These will print the transitions and the strip at every transition,
plus some more useful events.