# Copyright Dragan Dragos Ovidiu 2024-2025

The project consists of implementing the Map-Reduce paradigm, aimed at optimizing the search and concatenation of lists. To optimize resource management and synchronization, a single set of threads was used for both stages, consisting of M Map threads and R Reduce threads, which simplifies resource management.

## Thread Organization

- **M**: the number of threads for the mapping stage.
- **R**: the number of threads for the reducing stage.

----------

## Data Structures Used

- To avoid using global variables, I created two data structures, one for each type of thread: **MapThreadArgs** and **ReduceThreadArgs**. These structures allow passing the appropriate arguments to each type of thread.

----------

## Synchronization Mechanisms

To avoid **race condition** problems and ensure the correct operation of the program, the following mechanisms were used:

### 1. Main Barrier:
 - Used at the end of the `map_func` function to synchronize all Map threads.
 - Used at the beginning of the `reduce_func` function to synchronize all Reduce threads.

### 2. Reduce Barrier (reduce_barrier):
  - Synchronizes Reduce threads before the result is written to the output files.

### 3. Mutex
  - Creates an atomic zone for access to shared resources such as:
    - Input files.
    - `unordered_map` structure.

### 4. Vector of mutexes (`vector<mutex>`)
  - Allocates a mutex for each letter managed by an `unordered_map`, ensuring safe access.

------------

## Implementation

- To begin with, I created a priority queue for the files. To improve efficiency, the files are sorted by their size using `file_size`. As each thread finishes processing a file, it picks up the next one from the queue (Input files will be split as dynamically and evenly as possible).

Each thread has its own `unordered_map`, which eliminates the risk of a *race condition* that could occur if multiple threads tried to write simultaneously to the same memory area.

### `map_func` function

- Each thread picks a file from the queue and processes it, adding the words and the file number to its own `unordered_map`.
- If a thread finds two identical words from different files, it associates both files with the word `word = {file1, file2}`.
- The thread reads each word from the file and keeps only the words containing letters (excluding special characters or numbers).
- At the end of the process, a **main barrier** synchronizes the Map threads, waiting for their execution to finish.

### `reduce_func` function

- At the beginning of the `reduce_func` function, the **main barrier** is used to synchronize all Map and Reduce threads. Thus, the execution of Reduce threads starts only after all Map threads have finished processing.
- Each Reduce thread extracts an `unordered_map` created by a Map thread.
- In the first phase, the Reduce thread iterates over the `unordered_map` and inserts each word into another `unordered_map` associated with the first letter of the word. This is a vector containing 26 `unordered_map`s, one for each letter of the alphabet.
- To prevent concurrent access conflicts, I used a vector of 26 mutexes, one for each `unordered_map`. This ensures that two threads will not attempt to insert words into the same `unordered_map` simultaneously.
- A supplementary barrier (**reduce_barrier**) was introduced to synchronize all Reduce threads before starting the insertion of values into files.
- Finally, the words and associated files, resulting from the aggregation of lists, are printed simultaneously to the files. A `letter` counter is used to check if an `unordered_map` is empty (no data to print), in which case an empty file is created.

### `print_in_files` function

- In this function, I sort the list associated with each letter and create the corresponding file.
- The words are printed in the file in an order: first by the number of files associated with each word, and then alphabetically.
