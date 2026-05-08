#include <iostream>
using namespace std;
int add(int a, int b) {
    int result = a + b;
    return result;
}
int main() {
    int x = 5, y = 3;
    int sum = add(x, y);
    cout << x << " + " << y << " = " << sum << endl;
    return 0;
}