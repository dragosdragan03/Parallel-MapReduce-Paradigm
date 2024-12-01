#include <iostream>
#include <fstream>
#include <filesystem>
#include <pthread.h>
#include "pthread_barrier_mac.h"
#include <queue>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <set>
#include <algorithm> // Ensure this is included for std::remove
#include <unordered_map>

using namespace std;

struct CompareFiles {
    bool operator()(pair<string, int> f1, pair<string, int> f2) {
        uintmax_t sizeA = filesystem::file_size(f1.first); // Get file size of f1
        uintmax_t sizeB = filesystem::file_size(f2.first); // Get file size of f2
        return sizeA < sizeB;  // Sort in descending order by file size
    }
};

bool cmp(pair<string, set<int>> &a,
    pair<string, set<int>> &b)
{
    // first i sort by the number of elements from the list desc and after alphabetical
    int len_a = a.second.size();
    int len_b = b.second.size();

    if (len_a > len_b) {
        return true;
    } else if (len_b > len_a) {
        return false;
    } else {
        return a.first < b.first;
    }

}

struct MapThreadArgs {
    priority_queue<pair<string, int>, vector<pair<string, int>>, CompareFiles> *files;
    pthread_mutex_t *files_mutex;
    pthread_barrier_t *barrier;
    vector<unordered_map<string, set<int>>> *mappers;
    int thread_id = 0;
};

struct ReduceThreadsArgs {
    pthread_mutex_t *mutex; // this is a mutex to extract each unordered map
    pthread_barrier_t *barrier_all_threads;
    pthread_barrier_t *barrier_to_print;
    vector<unordered_map<string, set<int>>> *mappers;
    vector<unordered_map<string, set<int>>> *final_list; // each unorderd map has a letter (26 unordered map)
    vector<pthread_mutex_t> *letter_mutex; // this is a vector with mutexes for each letter (26 mutexes)
    int *letter; // i use this field to count if a letter is missing to create an empty file
};

priority_queue<pair<string, int>, vector<pair<string, int>>, CompareFiles> read_files(string file) {
    string line;
    ifstream f(file);

    if (!f.is_open()) {
        cout << "Fisierul " << file << " nu a putut fi deschis" << endl;
        return {}; // Return an empty priority_queue if the file can't be opened
    }

    getline(f, line); // This line represents the number of files (not used here)
    priority_queue<pair<string, int>, vector<pair<string, int>>, CompareFiles> pq;
    int count = 1;
    while (getline(f, line)) {
        line = "../checker/" + line;
        if (!filesystem::exists(line)) {
            cout << "Fisierul " << line << " nu exista" << endl;
            continue;
        }
        pq.push({ line, count });
        count++;
    }

    f.close();
    return pq; // Return the populated priority queue
}

void *map_func(void *args) {
    MapThreadArgs arg = *static_cast<MapThreadArgs *>(args);
    int thread_id = arg.thread_id;

    while (true) {
        pthread_mutex_lock(arg.files_mutex); // i have to lock to read a file from the pq
        if (arg.files->empty()) { // this means the pq is empty so i have to exit the loop
            pthread_mutex_unlock(arg.files_mutex);
            break;
        }
        pair<string, int> file = arg.files->top();
        ifstream f(file.first);
        arg.files->pop();
        pthread_mutex_unlock(arg.files_mutex);

        if (!filesystem::exists(file.first)) {
            cout << "Fisierul " << file.first << " nu exista" << endl;
            f.close();
            continue;
        }
        string word;

        while (f >> word) { // Read word by word
            for (auto &x : word) { // to make all the letter lower
                x = tolower(x);
            }
            string result;
            for (char c : word)
                if (isalpha(c))
                    result += c;

            word = result;
            if (!isalpha(word[0])) // first i have to verify that the word is not a number
                continue;
            (*arg.mappers)[thread_id][word].insert(file.second);
        }
        f.close();
    }

    pthread_barrier_wait(arg.barrier);
    pthread_exit(NULL);
}

void print_in_files(unordered_map<string, set<int>> input_set) {
    // first i have to sort the file and after to print

    vector<pair<string, set<int>>> sorted_list(input_set.begin(), input_set.end());
    sort(sorted_list.begin(), sorted_list.end(), cmp);

    char letter = sorted_list[0].first[0];
    string file_to_open = "../checker/" + string(1, letter) + ".txt";
    ofstream f(file_to_open);

    for (const auto &entry : sorted_list) {
        f << entry.first << ":[";

        int len_entry = entry.second.size(), i = 0;
        for (const auto iter : entry.second) {
            if (i != len_entry - 1)
                f << iter << " ";
            else
                f << iter;
            i++;
        }
        f << "]" << endl;
    }

    f.close();
}

void *reduce_function(void *args) {
    auto arg = *static_cast<ReduceThreadsArgs *>(args);
    pthread_barrier_wait(arg.barrier_all_threads);

    while (true) {
        pthread_mutex_lock(arg.mutex);
        if ((*arg.mappers).empty()) {
            pthread_mutex_unlock(arg.mutex);
            break;
        }
        unordered_map<string, set<int>> set = (*arg.mappers).back();
        (*arg.mappers).pop_back();
        pthread_mutex_unlock(arg.mutex);

        for (const auto &entry : set) {
            char first_letter = entry.first[0]; // this is the first letter to identify where to insert the word
            pthread_mutex_lock(&(*arg.letter_mutex)[first_letter - 'a']);
            (*arg.final_list)[first_letter - 'a'][entry.first].insert(entry.second.begin(), entry.second.end());
            pthread_mutex_unlock(&(*arg.letter_mutex)[first_letter - 'a']);
        }
    }

    pthread_barrier_wait(arg.barrier_to_print); // i'm waiting all threads to finish

    // i want each thread to print a letter in file
    while (true) {
        pthread_mutex_lock(arg.mutex);
        int current_letter = (*arg.letter);
        (*arg.letter)--;
        if ((*arg.final_list).empty()) {
            pthread_mutex_unlock(arg.mutex);
            pthread_exit(NULL);
            break;
        }
        unordered_map<string, set<int>> set = (*arg.final_list).back();
        (*arg.final_list).pop_back();
        pthread_mutex_unlock(arg.mutex);

        if (set.empty()) {
            string file_to_open = "../checker/" + string(1, static_cast<char>(current_letter + 'a')) + ".txt";
            ofstream f(file_to_open);
            f.close();
            continue;
        }
        print_in_files(set);
    }

    pthread_exit(NULL); // Ensure the function returns a pointer of type void*
}

int main(int argc, char const *argv[]) {

    if (argc < 4) {
        cout << "Not enough arguments" << endl;
        return 0;
    }

    int nr_thread_map = 0, nr_thread_red = 0, ret, letter = 25;
    pthread_mutex_t mutex;
    vector<pthread_mutex_t> letter_mutex(26);

    pthread_barrier_t barrier; // this is a barrier for all threads
    pthread_barrier_t reduce_barrier; // this is a barrier for reduce threads to wait until all threads finish to complete the unordered_maps

    pthread_mutex_init(&mutex, NULL);
    for (int i = 0; i < 26; i++) {
        pthread_mutex_init(&letter_mutex[i], NULL);
    }

    priority_queue<pair<string, int>, vector<pair<string, int>>, CompareFiles> files;
    vector<unordered_map<string, set<int>>> final_list(26);

    nr_thread_map = atoi(argv[1]);
    nr_thread_red = atoi(argv[2]);
    int num_threads = nr_thread_map + nr_thread_red;
    vector<unordered_map<string, set<int>>> mappers(nr_thread_map);

    pthread_barrier_init(&barrier, NULL, num_threads);
    pthread_barrier_init(&reduce_barrier, NULL, nr_thread_red); // i make this barrier to wait the number of threads for reduce
    pthread_t threads[num_threads];

    if (!filesystem::exists(argv[3])) {
        cout << "Fisierul " << argv[3] << " nu exista" << endl;
        return 1;
    }

    files = read_files(argv[3]);
    vector<MapThreadArgs> thread_args(nr_thread_map);
    vector<ReduceThreadsArgs> reduce_thread_args(nr_thread_red);

    for (int id = 0; id < num_threads; id++) {
        if (id < nr_thread_map) {
            thread_args[id] = { &files, &mutex, &barrier, &mappers, id };
            ret = pthread_create(&threads[id], NULL, &map_func, &thread_args[id]);
        } else {
            reduce_thread_args[id - nr_thread_map] = { &mutex, &barrier ,&reduce_barrier, &mappers, &final_list, &letter_mutex, &letter };
            ret = pthread_create(&threads[id], NULL, &reduce_function, &reduce_thread_args[id - nr_thread_map]);
        }

        if (ret) {
            cout << "Eroare la crearea threadului";
            exit(-1);
        }
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    // destroy the mutexes
    pthread_mutex_destroy(&mutex);
    for (int i = 0; i < letter_mutex.size(); i++) {
        pthread_mutex_destroy(&letter_mutex[i]);
    }

    // destroy the barriers
    pthread_barrier_destroy(&barrier);
    pthread_barrier_destroy(&reduce_barrier);


    return 0;
}