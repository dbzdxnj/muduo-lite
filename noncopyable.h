# pragma once

//nocopyable被继承后，派生类对象可以正常进行构造和析构，但是不能进行拷贝构造和赋值；
class noncopyable
{
protected:
    noncopyable() = default;
    ~noncopyable() = default;
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;
};


