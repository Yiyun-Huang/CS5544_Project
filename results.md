# Evaluation Results:

### micro benchmarks (comparing average points to set size, must/may alias pairs):

## test.c
**Test description**: This test mirrors the example from the lecture 14 slides. The purpose of this test is to demonstrate correctness for a simple case of pointer analysis. 
### [flow-sensitive]
Points-to relationships upon exit:

| Pointer | Abstract Location(s) |
| -------- | -------- |
| b |{ call1 }
| a  | { call1; call3 } | 

Evaluation Metrics [flow-sensitive] for function 'main'
| Metric | Value |
| -------- | -------- |
| pointer variables tracked at exit | 2| 
| avg points-to set size at exit   | 1.500 | 
| may-alias pairs   (any point)  | 1 | 
| must-alias pairs   (any point)  | 1 | 
| analysis time   | 9.618 ms | 
| program points analyzed    | 19 | 
| abstract objects     | 6 | 
| worklist iterations     | 37 | 



### [flow-insensitive]
Points-to relationships upon exit:

| Pointer | Abstract Location(s) |
| -------- | -------- |
| b | { call1 } | 
| a  | { call; call1; call2; call3 } | 

Evaluation Metrics [flow-insensitive] for function 'main'
| Metric | Value |
| -------- | -------- |
| pointer variables tracked at exit | 2| 
| avg points-to set size at exit   | 2.500 | 
| may-alias pairs   (any point)  | 1 | 
| must-alias pairs   (any point)  | 0 | 
| analysis time   | 0.193 ms | 
| program points analyzed    | 19 | 
| abstract objects     | 6 | 
| fixpoint iterations     | 2 | 


## test2.c
**Test description**: This test includes examples from the KSK textbook (page 122) as well as the CFG from Assignment 3 part 2. The KSK example demonstrates correctness in the context of must/may points to sets as well as in the overall context of the KSK Data-Flow Analysis for flow sensitive points to analysis. Both of these examples demonstrate the increased accuracy of the flow-sensitive analysis at the cost of increased analysis time. 
### [flow-sensitive] 
#### Function: main
Points-to relationships upon exit:
| Pointer | Abstract Location(s) |
| -------- | -------- |
| r | { call2; call3 } | 
| p  | { call2; call3 } | 
| q  | { call2 } | 

Evaluation Metrics [flow-sensitive] for function 'main'
| Metric | Value |
| -------- | -------- |
| pointer variables tracked at exit | 3| 
| avg points-to set size at exit   | 1.667 | 
| may-alias pairs   (any point)  | 3 | 
| must-alias pairs   (any point)  | 3 | 
| analysis time   | 33.595 ms | 
| program points analyzed    | 34 | 
| abstract objects     | 10 | 
| worklist iterations     | 69 | 


#### Function: func2
Points-to relationships upon exit:
| Pointer | Abstract Location(s) |
| -------- | -------- |
| a | { d; b } | 
| b  | { d; b } | 
| c  | { d; b } | 

Evaluation Metrics [flow-sensitive] for function 'func2'
| Metric | Value |
| -------- | -------- |
| pointer variables tracked at exit | 3| 
| avg points-to set size at exit   | 2.000 | 
| may-alias pairs   (any point)  | 3 | 
| must-alias pairs   (any point)  | 0 | 
| analysis time   | 30.090 ms | 
| program points analyzed    | 27 | 
| abstract objects     | 7 | 
| worklist iterations     | 82 | 


### [flow-insensitive] 
#### Function: main
Points-to relationships upon exit:

| Pointer | Abstract Location(s) |
| -------- | -------- |
| q  | { call; call1; call2; call3 } | 
| r | { call; call1; call2; call3 } | 
| p  | { call; call1; call2; call3 } | 

Evaluation Metrics [flow-insensitive] for function 'main'
| Metric | Value |
| -------- | -------- |
| pointer variables tracked at exit | 3| 
| avg points-to set size at exit   | 4.000 | 
| may-alias pairs   (any point)  | 3 | 
| must-alias pairs   (any point)  | 0 | 
| analysis time   | 0.193 ms | 
| program points analyzed    | 34 | 
| abstract objects     | 10 | 
| fixpoint iterations     | 4 | 


#### Function: func2
Points-to relationships upon exit:

| Pointer | Abstract Location(s) |
| -------- | -------- |
| a | { d; b; c }| 
| b  | { d; b; c }| 
| c  | { d; b; c }| 
| d  | { d; b; c } |

Evaluation Metrics [flow-insensitive] for function 'func2'
| Metric | Value |
| -------- | -------- |
| pointer variables tracked at exit | 4| 
| avg points-to set size at exit   | 4.000 | 
| may-alias pairs   (any point)  | 6 | 
| must-alias pairs   (any point)  | 0 | 
| analysis time   | 0.063 ms | 
| program points analyzed    | 27 | 
| abstract objects     | 7 | 
| fixpoint iterations     | 4 | 


## complex_pointer_examples.c
**Test description**: The purpose of this test is to demonstrate that the flow-sensitive analysis handles all six pointer scenarios. More information on this test can be found in `complex-pointer-examples.md`. This test does not demonstrate any increased accuracy in points-to sets since there are no non-trivial branches in the test's cfg. 
### [flow-sensitive]
Points-to relationships upon exit:
| Pointer | Abstract Location(s) |
| -------- | -------- |
| b | { x }| 
| c | { b }| 
| g | { x; y }| 
| a | { x }| 
| f | { x;y }| 
| d | { f }| 
| e | { x }| 

Evaluation Metrics [flow-sensitive] for function 'main'
| Metric | Value |
| -------- | -------- |
| pointer variables tracked at exit | 7| 
| avg points-to set size at exit   | 1.286 | 
| may-alias pairs   (any point)  | 10 | 
| must-alias pairs   (any point)  | 6 | 
| analysis time   | 29.530 ms | 
| program points analyzed    | 26 | 
| abstract objects     | 9 | 
| worklist iterations     | 48 | 


### [flow-insensitive]
Points-to relationships upon exit:
| Pointer | Abstract Location(s) |
| -------- | -------- |
| b | { x }| 
| c | { b }| 
| g | { x; y }| 
| a | { x }| 
| f | { x;y }| 
| d | { f }| 
| e | { x }| 

Evaluation Metrics [flow-sensitive] for function 'main'
| Metric | Value |
| -------- | -------- |
| pointer variables tracked at exit | 7| 
| avg points-to set size at exit   | 1.286 | 
| may-alias pairs   (any point)  | 10 | 
| must-alias pairs   (any point)  | 6 | 
| analysis time   | 0.268 ms | 
| program points analyzed    | 26 | 
| abstract objects     | 9 | 
| fixpoint iterations     | 2 | 



## iterative_example.c
**Test description**: This is a relativly trivial test to demonstrate the correctness of the iterative analysis in a simple case. A more interesting test of the iterative DFA takes place in the function `func2` in the `test2.c` test.
### [flow-sensitive]
Points-to relationships upon exit:
| Pointer | Abstract Location(s) |
| -------- | -------- |
| a | { call1; call2 }| 
| b | { call }| 

Evaluation Metrics [flow-sensitive] for function 'main'
| Metric | Value |
| -------- | -------- |
| pointer variables tracked at exit | 2| 
| avg points-to set size at exit   | 1.500 | 
| may-alias pairs   (any point)  | 0 | 
| must-alias pairs   (any point)  | 0 | 
| analysis time   | 28.241 ms | 
| program points analyzed    | 22 | 
| abstract objects     | 5 | 
| worklist iterations     | 56 | 


### [flow-insensitive]
Points-to relationships upon exit:
| Pointer | Abstract Location(s) |
| -------- | -------- |
| a | { call1; call2 }| 
| b | { call }| 

Evaluation Metrics [flow-sensitive] for function 'main'
| Metric | Value |
| -------- | -------- |
| pointer variables tracked at exit | 2| 
| avg points-to set size at exit   | 1.500 | 
| may-alias pairs   (any point)  | 0 | 
| must-alias pairs   (any point)  | 0 | 
| analysis time   | 0.192 ms | 
| program points analyzed    | 22 | 
| abstract objects     | 5 | 
| fixpoint iterations     | 2 | 


### macro benchmarks (comparing analysis time to program size)

Note: in test output log files, these metrics are reported per-function. Here they are reported as a sum for the entire benchmark program.

## tree-ops.c

**Evaluation Metrics [flow-sensitive]**

| Metric | Value |
| -------- | -------- |
| analysis time   | 217.521 ms | 
| program points analyzed   | 294 | 
| abstract objects  | 143 | 

**Evaluation Metrics [flow-insensitive]**

| Metric | Value |
| -------- | -------- |
| analysis time   | 0.228 ms | 
| program points analyzed   | 294 | 
| abstract objects  | 143 | 

## linked-list.c

**Evaluation Metrics [flow-sensitive]**

| Metric | Value |
| -------- | -------- |
| analysis time   | 118.69 ms | 
| program points analyzed   | 140 | 
| abstract objects  | 82 | 

**Evaluation Metrics [flow-insensitive]**

| Metric | Value |
| -------- | -------- |
| analysis time   | 0.315 ms | 
| program points analyzed   | 140 | 
| abstract objects  | 82 | 


## Steensgaard tests:

### steensgaard-test.c
**Test description**: This test represents a pathological example and is meant to demonstrate the speedup that the Steensgaard algorithm provides when compared to the Andersen algorithm. This test mirrors the example on slide 20 of the Lecutre 14 Handwritten notes. 

Metrics to compare: 

Averaging the analysis time over five runs, 
Steensgaard: 0.2242 ms
Andersen: 0.6906 ms


### steensgaard-accuracy-test.c
**Test description**: This test compares the accuracy of Steensgaard to Andersen and our Flow Sensitive analysis.

Flow Sensitive output: 
  s: { e }
  r: { d }
  q: { c }
  p: { b }
  t: { e }

Flow Insensitive output: 
  p: { a; b; c; d; e }
  t: { e }
  q: { b; c; d; e }
  r: { c; d; e }
  s: { d; e }

Steensgaard output: 
Class #0: { t } -> pts -> { d, a, e, c, b }
Class #1: { s } -> pts -> { d, a, e, c, b }
Class #2: { r } -> pts -> { d, a, e, c, b }
Class #3: { q } -> pts -> { d, a, e, c, b }
Class #4: { p } -> pts -> { d, a, e, c, b }


As you can see, Steensgaard is the least accurate of the three analyses.