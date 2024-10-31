#ifndef SANDBOX_HPP
#define SANDBOX_HPP

#include <iostream>
#include <utility>

// namespace py = pybind11;

struct Messenger
{
    explicit Messenger(std::string name_ = "DefaultObject") : name(std::move(name_)) { std::cout << "CTOR - " << name << "\n"; }
    ~Messenger() { std::cout << "DTOR - " << (name.empty() ? "moved away..." : name) << "\n"; }
    Messenger(const Messenger& other) : name(other.name)
    {
        std::cout << "COPY - " << name << "\n";
        if (not name.empty())
        {
            try
            {
                auto subs = name.substr(name.size() - 1, 1);
                auto order = std::stoi(subs);
                name = name.substr(0, name.size() - 1) + std::to_string(order + 1);
            }
            catch (const std::invalid_argument&)
            {
                name += std::to_string(2);
            }
        }
    }
    Messenger(Messenger&& other) noexcept : name(std::move(other.name)) { std::cout << "MOVE - " << name << "\n"; }
    Messenger& operator=(const Messenger& other)
    {
        if (this == &other)
        {
            return *this;
        }
        name = other.name;
        std::cout << "COPY assignment - " << name << "\n";
        return *this;
    }
    Messenger& operator=(Messenger&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }
        name = std::move(other.name);
        std::cout << "MOVE assignment - " << name << "\n";
        return *this;
    }

    std::string name;
};

#endif  // SANDBOX_HPP
