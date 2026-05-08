#include<iostream>
#include<string>
using namespace std;
class Person{
public:
    int age;
    string name;
    
    void say(){
        cout << "我是" << name << ",今年" << age << "岁。" << endl;
    }
};
int main(){
    Person p;
    p.name = "张三";
    p.age = 10000;
    p.say();
    return 0;
}