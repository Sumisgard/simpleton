#include <iostream>
#include <thread>
#include <chrono>
using namespace std;

void task1() {
    cout << "Thread 1 is running. ID: " << this_thread::get_id() << "\n";
}

void task2() {
    cout << "Thread 2 is running. ID: " << this_thread::get_id() << "\n";
}

int main() {
    thread t1(task1);
    thread t2(task2);

    // Get thread IDs
    cout << "t1 ID: " << t1.get_id() << "\n";
    cout << "t2 ID: " << t2.get_id() << "\n";

    // Join t1 if joinable
    if (t1.joinable()) {
        t1.join();
        cout << "t1 joined\n";
    }

    // Detach t2 if joinable
    if (t2.joinable()) {
        t2.detach();
        cout << "t2 detached\n";
    }

    // Give detached thread time to complete
    this_thread::sleep_for(chrono::milliseconds(100));

    cout << "Main thread finished.\n";
    return 0;
}