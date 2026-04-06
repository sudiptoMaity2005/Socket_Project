#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h> 

void generate_primes(int n) {
    if (n < 2) {
        printf("No prime numbers up to %d\n", n);
        return;
    }

    bool *is_prime = (bool *)malloc((n + 1) * sizeof(bool));
    for (int i = 0; i <= n; i++) is_prime[i] = true;

    is_prime[0] = false;
    is_prime[1] = false;

    for (int p = 2; p * p <= n; p++) {
        if (is_prime[p] == true) {
            for (int i = p * p; i <= n; i += p) {
                is_prime[i] = false;
            }
        }
    }

    printf("Primes up to %d are:\n", n);
    for (int p = 2; p <= n; p++) {
        if (is_prime[p]) {
            printf("%d ", p);
        }
    }
    printf("\n");

    free(is_prime); 
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Error: Missing limit argument from server.\n");
        return 1;
    }
    
    int n = atoi(argv[1]); 
    generate_primes(n);
    
    return 0;
}