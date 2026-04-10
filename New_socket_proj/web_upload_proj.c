#include <stdio.h>
#include <stdbool.h>
#include <math.h>

// Function to check if a number is prime
bool is_prime(int n) {
    if (n < 2) return false;
    if (n == 2 || n == 3) return true;
    if (n % 2 == 0 || n % 3 == 0) return false;
    
    // Check for factors up to the square root for efficiency
    for (int i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0)
            return false;
    }
    return true;
}

int main() {
    int count = 0;   // Number of primes found
    int num = 2;     // Number to test for primality

    printf("The first 1000 prime numbers are:\n");

    while (count < 1000) {
        if (is_prime(num)) {
            printf("%d ", num);
            count++;
            
            // Print a newline every 10 numbers for readability
            if (count % 10 == 0) printf("\n");
        }
        num++;
    }

    return 0;
}
