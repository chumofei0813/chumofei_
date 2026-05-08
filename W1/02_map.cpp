#include<iostream>
#include<map>
#include<string>
using namespace std;
int main(){
    map<string,int>m;
    m["张三"]=100;
    m["李四"]=0;
    cout << m["张三"] << endl;
    cout << m["李四"] << endl;
    return 0;
}