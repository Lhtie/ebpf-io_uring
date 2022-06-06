#include <bits/stdc++.h>
using namespace std;

int main(){
    freopen("dict.txt", "w", stdout);
    srand(time(0));
    long long n = 128 * 1024 * 1024;
    while (n--){
        int op = rand()%96;
        if (op == 0){
            printf("%c", char(13));
        } else {
            printf("%c", char(rand()%95 + 32));
        }
    }
}