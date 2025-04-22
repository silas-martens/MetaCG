class Base {
public:
    virtual void foo() {
    }
};

class Derived : public Base {
public:
    void foo() override {
    }
};

int main() {
    Base* b = new Derived();
    b->foo();  
    delete b;
    return 0;
}
