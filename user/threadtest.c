/**
 * TocinOS Threading Test
 * 
 * Tests the pthread library with basic thread operations:
 * - Thread creation
 * - Mutex synchronization
 * - Thread join
 */

#include "libc/pthread.h"
#include "libc/stdio.h"

// Shared counter protected by mutex
volatile int counter = 0;
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

// Thread function - increment counter
void *increment_thread(void *arg) {
    int thread_num = (int)(long)arg;
    
    printf("Thread %d starting\n", thread_num);
    
    for (int i = 0; i < 1000; i++) {
        pthread_mutex_lock(&counter_mutex);
        counter++;
        pthread_mutex_unlock(&counter_mutex);
    }
    
    printf("Thread %d finished\n", thread_num);
    
    return (void *)(long)thread_num;
}

// Thread function - compute factorial
void *factorial_thread(void *arg) {
    int n = (int)(long)arg;
    int result = 1;
    
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    
    printf("factorial(%d) = %d\n", n, result);
    
    return (void *)(long)result;
}

int main(void) {
    printf("\n=== TocinOS Threading Test ===\n\n");
    
    // Test 1: pthread_self
    printf("Test 1: pthread_self()\n");
    pthread_t self = pthread_self();
    printf("  Main thread ID: %lu\n\n", (unsigned long)self);
    
    // Test 2: Create a single thread
    printf("Test 2: Single thread creation\n");
    pthread_t thread1;
    int ret = pthread_create(&thread1, NULL, factorial_thread, (void *)5L);
    if (ret == 0) {
        printf("  Created thread: %lu\n", (unsigned long)thread1);
        
        void *retval;
        pthread_join(thread1, &retval);
        printf("  Thread returned: %ld\n\n", (long)retval);
    } else {
        printf("  Failed to create thread: %d\n\n", ret);
    }
    
    // Test 3: Mutex operations
    printf("Test 3: Mutex operations\n");
    pthread_mutex_t test_mutex;
    pthread_mutex_init(&test_mutex, NULL);
    
    if (pthread_mutex_trylock(&test_mutex) == 0) {
        printf("  trylock succeeded\n");
        pthread_mutex_unlock(&test_mutex);
        printf("  unlock succeeded\n");
    }
    
    pthread_mutex_lock(&test_mutex);
    printf("  lock succeeded\n");
    
    if (pthread_mutex_trylock(&test_mutex) != 0) {
        printf("  trylock on locked mutex correctly failed\n");
    }
    
    pthread_mutex_unlock(&test_mutex);
    pthread_mutex_destroy(&test_mutex);
    printf("  destroy succeeded\n\n");
    
    // Test 4: Multiple threads with shared counter
    printf("Test 4: Multiple threads with mutex\n");
    counter = 0;
    
    #define NUM_THREADS 4
    pthread_t threads[NUM_THREADS];
    
    printf("  Creating %d threads...\n", NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; i++) {
        ret = pthread_create(&threads[i], NULL, increment_thread, (void *)(long)i);
        if (ret != 0) {
            printf("  Failed to create thread %d\n", i);
        }
    }
    
    printf("  Waiting for threads to complete...\n");
    for (int i = 0; i < NUM_THREADS; i++) {
        void *retval;
        pthread_join(threads[i], &retval);
        printf("  Thread %d joined (returned %ld)\n", i, (long)retval);
    }
    
    printf("  Final counter value: %d (expected %d)\n\n", 
           counter, NUM_THREADS * 1000);
    
    // Test 5: Thread-Local Storage
    printf("Test 5: Thread-Local Storage\n");
    pthread_key_t key;
    ret = pthread_key_create(&key, NULL);
    if (ret == 0) {
        printf("  Created TLS key: %u\n", key);
        
        pthread_setspecific(key, (void *)12345L);
        void *value = pthread_getspecific(key);
        printf("  Stored and retrieved value: %ld\n", (long)value);
        
        pthread_key_delete(key);
        printf("  Deleted key\n");
    }
    printf("\n");
    
    // Test 6: pthread_once
    printf("Test 6: pthread_once\n");
    static int init_count = 0;
    void init_func(void) {
        init_count++;
        printf("  init_func called (count=%d)\n", init_count);
    }
    
    pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, init_func);
    pthread_once(&once, init_func);
    pthread_once(&once, init_func);
    printf("  init_func should have been called exactly once\n\n");
    
    printf("=== All threading tests complete ===\n");
    
    return 0;
}
