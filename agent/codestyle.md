# C++ Code Style and Development Rules

## CS-01: Paradigm

### Combining OOP and FP

Development uses a combination of object-oriented and functional approaches. From OOP we take object design and data encapsulation, from FP — clarity of data processing flow, pure functions, and immutability.

Principles:
- **Classes** encapsulate data and provide methods for working with it
- **Data processing functions** should be pure: accept data, return result, do not mutate input arguments
- **Transformation methods** return a new object instead of modifying the current one
- **Function composition** is used to build processing chains: filtering -> transformation -> aggregation

```cpp
// OOP: data encapsulation
class Order
{
public:
    /// @brief Creates order with given id and amount
    static std::tuple<OrderPtr, error> Create(int id, double amount);

    /// @brief Returns order id
    int GetId() const;
    /// @brief Returns order amount
    double GetAmount() const;
    /// @brief Returns payment status
    bool GetIsPaid() const;

    /// @brief Marks order as paid
    void MarkAsPaid();

    /// @brief Returns new order with discounted amount (does not modify current object)
    Order WithDiscount(double percentage) const;

    // ...
};

// FP: pure static functions for collection processing
class OrderProcessor
{
public:
    /// @brief Filters unpaid orders (pure function)
    static std::vector<Order> FilterUnpaid(const std::vector<Order> & orders);

    /// @brief Applies discount to all orders, returns new collection (pure function)
    static std::vector<Order> ApplyDiscount(const std::vector<Order> & orders, double discount);

    /// @brief Calculates total amount (pure function)
    static double CalculateTotal(const std::vector<Order> & orders);

    /// @brief Composes processing pipeline: filter -> discount -> total
    static double ProcessOrders(const std::vector<Order> & orders);
};

// Usage: clear processing flow
double total = OrderProcessor::ProcessOrders(orders);
```

### Single Responsibility Principle

Every class and every function should have one responsibility. A class is responsible for one entity or one task. A function performs one operation. If the name of a class or function contains a conjunction "and" or lists actions — this is a sign of violating the principle.

```cpp
// Bad: class combines business logic and formatting
class ReportGenerator
{
public:
    std::tuple<Report, error> GenerateReport(const Data & data);
    error SendReportByEmail(const Report & report, const std::string & recipient);
    std::string FormatReportAsHtml(const Report & report);
};

// Good: each class — one responsibility
class ReportGenerator
{
public:
    std::tuple<Report, error> Generate(const Data & data);
};

class ReportFormatter
{
public:
    static std::string ToHtml(const Report & report);
    static std::string ToCsv(const Report & report);
};

class ReportSender
{
public:
    static error SendByEmail(const Report & report, const std::string & recipient);
};
```

For functions — similarly. A function should perform one action. If a sequence of steps is required — it should be wrapped in a function with a name reflecting the unified meaning of that sequence.

```cpp
// Bad: function name lists actions
error ValidateAndSaveConfig(const Config & config);
error ParseAndExecuteCommand(const std::string & input);

// Good: one action — one function
error ValidateConfig(const Config & config);
error SaveConfig(const Config & config);

// Good: if a sequence is needed — the name reflects the unified goal
error ApplyConfig(const Config & config);
```

## CS-02: C++ Language Usage

The main thesis — do not use complex language features for their own sake. Apply only those C++ capabilities that do not harm readability and simplify architecture.

### `auto`, `const`, `constexpr`

Use `auto` for type deduction wherever possible. Use `const` for all variables and parameters that are not modified. Use `constexpr` for all values computable at compile time.

```cpp
// Bad: explicit types, missing const
std::string name = config.GetName();
std::vector<Order> orders = store::GetOrders();
int maxSize = 1024;

// Good: auto, const, constexpr
const auto name = config.GetName();
const auto orders = store::GetOrders();
constexpr auto maxSize = 1024;
```

### Range-based for

For iterating over collections always use range-based `for`. Elements are passed by `const auto &` if modification is not required.

```cpp
// Bad: index-based iteration without necessity
for (std::size_t i = 0; i < orders.size(); ++i) {
    logging::Log("Order: {}", orders[i].GetId());
}

// Good: range-based for
for (const auto & order : orders) {
    logging::Log("Order: {}", order.GetId());
}
```

### Lambda expressions

Lambda expressions are acceptable where they do not hinder code understanding and do not violate its structure. Suitable for short callbacks, STL algorithms, and grouping local sequences of operations.

```cpp
// Good: lambda in STL algorithm
const auto unpaid = std::views::filter(orders, [](const auto & order) {
    return !order.GetIsPaid();
});

// Good: lambda for grouping fallible operations
const auto runSetup = [&module]() -> error {
    // Initialize module resources
    auto err = device::InitResources(module);
    if (err) {
        return errors::Wrap(err, "Failed to init resources");
    }

    // Apply module configuration
    err = device::ApplyConfig(module);
    if (err) {
        return errors::Wrap(err, "Failed to apply config");
    }

    return nullptr;
};
```

### STL and proven libraries

Use STL and well-established libraries (vcpkg) instead of writing custom implementations. Do not reinvent the wheel for data structures, algorithms, threading, etc.

```cpp
// Bad: custom search implementation
bool found = false;
for (const auto & item : items) {
    if (item.GetId() == targetId) {
        found = true;
        break;
    }
}

// Good: STL algorithm
const auto found = std::ranges::any_of(items, [&targetId](const auto & item) {
    return item.GetId() == targetId;
});
```

### No magic numbers

Literal numeric values in code are prohibited. All numeric constants must be named via `constexpr` or `inline constexpr` in the appropriate namespace.

```cpp
// Bad: magic numbers
if (retryCount > 3) {
    return errors::New("Too many retries");
}

std::this_thread::sleep_for(std::chrono::milliseconds(500));

// Good: named constants
namespace policy
{
inline constexpr auto MaxRetries = 3;
inline constexpr auto RetryDelay = std::chrono::milliseconds(500);
}  // namespace policy

if (retryCount > policy::MaxRetries) {
    return errors::New("Too many retries");
}

std::this_thread::sleep_for(policy::RetryDelay);
```

### Mandatory braces

Single-line `if`, `else`, `for`, `while` without braces are prohibited. The body of control constructs is always wrapped in `{}`, even if it contains one expression.

```cpp
// Bad: without braces
if (err)
    return err;

for (const auto & item : items)
    pipeline::Process(item);

if (status.IsReady())
    device::Start(module);
else
    device::Reset(module);

// Good: braces always
if (err) {
    return err;
}

for (const auto & item : items) {
    pipeline::Process(item);
}

if (status.IsReady()) {
    device::Start(module);
} else {
    device::Reset(module);
}
```

### No exceptions

Exceptions (`throw`, `try`/`catch`) are not used. For error handling the approach with returning `error` is applied (see "Error Handling" section).

```cpp
// Bad: exceptions
std::tuple<Config, error> ParseConfig(const std::string & path)
{
    try {
        auto data = io::ReadFile(path);
        return {Config{data}, nullptr};
    } catch (const std::exception & exception) {
        return { {}, errors::New(exception.what())};
    }
}

// Good: error return
std::tuple<Config, error> ParseConfig(const std::string & path)
{
    // Read config file
    auto [data, err] = io::ReadFile(path);
    if (err) {
        return { {}, errors::Wrap(err, "Failed to read config")};
    }

    return {Config{data}, nullptr};
}
```

## CS-03: Error Handling

### Returning errors from functions

For error handling the [errors](https://github.com/fogesque/errors) library is used. The type `error` is an alias for `std::shared_ptr<errors::Error>`. Any function that can technically fail must return `error`. Successful execution is indicated by returning `nullptr`, an error — by a non-null pointer.

If a function returns data along with an error, `std::tuple` and structured bindings are used. The error object is always last in the tuple.

```cpp
#include <errors/errors.hpp>

error SaveConfig(const Config & config)
{
    // Open config file for writing
    auto [file, err] = io::OpenFile(config.path);
    if (err) {
        return errors::Wrap(err, "Failed to save config");
    }

    // Write config data to file
    err = io::WriteData(file, config.data);
    if (err) {
        return errors::Wrap(err, "Failed to save config");
    }

    return nullptr;
}
```

### Error message format

Every error message must start with a capital letter.

```cpp
// Bad: message starts with lowercase
return errors::New("connection refused");
return errors::Wrap(err, "failed to connect");

// Good: message starts with capital letter
return errors::New("Connection refused");
return errors::Wrap(err, "Failed to connect");
return errors::Errorf("Device '{}' not found on port {}", device.Name(), port);
```

### Code flow in error handling

Every fallible function call is separated by an empty line from the previous block. A comment is placed before the call describing the logical step. This reduces visual clutter. Only early return (`if (err) { return ... }`) is used — `else` constructs are not applied.

```cpp
error InitializeModule(DeviceModulePtr module)
{
    // Setup module with default configuration
    auto err = device::SetupModule(module);
    if (err) {
        return errors::Wrap(err, "Failed to setup module");
    }

    // Start module after successful setup
    err = device::StartModule(module);
    if (err) {
        return errors::Wrap(err, "Failed to start module");
    }

    // Get module status after its start
    auto [status, err2] = device::GetModuleStatus(module);
    if (err2) {
        return errors::Wrap(err2, "Failed to get module status");
    }

    logging::Log("Module status: {}", status.Message());
    return nullptr;
}
```

## CS-04: Function Design

### Function purity and return values

Functions should follow the principle of purity: not affect external objects and not use output parameters in arguments. All results are returned through the return value.

If a function returns multiple objects, `std::tuple` is used. Using `std::pair` is prohibited — always `std::tuple`, even for two elements. If a function can fail, the `error` object is always the last element of the tuple.

```cpp
// Bad: output parameter in arguments
error ParseConfig(const std::string & path, Config & out_config);

// Bad: std::pair
std::pair<Config, error> ParseConfig(const std::string & path);

// Good: std::tuple, error last
std::tuple<Config, error> ParseConfig(const std::string & path);

// Good: multiple return values
std::tuple<Host, Port, error> ParseAddress(const std::string & address);
```

Calling such functions with structured bindings:

```cpp
auto [config, err] = parser::ParseConfig("/etc/app/config.json");
if (err) {
    return errors::Wrap(err, "Failed to parse config");
}

auto [host, port, err2] = network::ParseAddress(config.address);
if (err2) {
    return errors::Wrap(err2, "Failed to parse address");
}
```

### Limiting parameter and return value count

The argument list of functions and constructors must not exceed three elements. If more are required — parameters are packed into structures, grouped by meaning.

Similarly for return tuples: maximum two values (not counting `error`). If more need to be returned — the result is packed into a structure.

```cpp
// Bad: too many arguments
error CreateConnection(const std::string & host, int port, int timeout,
                       bool useTls, int maxRetries);

// Good: parameters grouped into structure
struct ConnectionConfig {
    std::string host;
    int port = 0;
    int timeout = 0;
    bool useTls = false;
    int maxRetries = 0;
};

error CreateConnection(const ConnectionConfig & config);

// Bad: too many values in tuple (3 values + error)
std::tuple<Host, Port, Protocol, error> ParseEndpoint(const std::string & endpoint);

// Good: result packed into structure
struct Endpoint {
    Host host;
    Port port;
    Protocol protocol;
};

std::tuple<Endpoint, error> ParseEndpoint(const std::string & endpoint);

// Good: two values + error is acceptable
std::tuple<Host, Port, error> ParseAddress(const std::string & address);
```

### Smart pointers

Class objects are always created and passed via `std::shared_ptr`. Every class must have a corresponding type alias `ClassNamePtr`. Raw pointers (`new`/`delete`) are not used.

```cpp
// Bad: raw pointers
auto module = new DeviceModule();
delete module;

// Good: smart pointers via factory method
auto [module, err] = DeviceModule::Create(config);

// Good: passing via smart pointer
error StartModule(DeviceModulePtr module);
```

## CS-05: Naming

### Class method naming

Public methods are named in PascalCase — starting with a capital letter. Private methods are named in camelCase — starting with a lowercase letter.

```cpp
class ConnectionPool
{
public:
    std::tuple<Connection, error> Acquire();
    error Release(Connection connection);
    std::size_t Size() const;

private:
    error validateConnection(const Connection & connection);
    void removeExpired();
};
```

### Interface naming

Interface classes (containing only pure virtual methods) are named with the `I` prefix. A class may implement multiple interfaces.

```cpp
// Module interface
class IModule
{
public:
    virtual error Start() = 0;
    virtual void Stop() = 0;
};

// Property interface
class IAwaitable
{
public:
    virtual Awaitable Await() = 0;
};

// Implementing multiple interfaces
class DeviceModule : public IModule, public IAwaitable
{
public:
    error Start() override;
    void Stop() override;
    Awaitable Await() override;
};
```

### Member naming and access

Class members are named without suffixes (`_`) and prefixes (`m_`) — with plain names. For accessing any members and methods of the class, `this->` is always used.

```cpp
// Bad: suffix, prefix, access without this
class Server
{
private:
    std::string m_host;
    int port_;

    void setup()
    {
        m_host = "localhost";
        port_ = 8080;
    }
};

// Good: plain names, access via this->
class Server
{
public:
    error Start();

private:
    std::string host;
    int port = 0;

    void setup()
    {
        this->host = "localhost";
        this->port = 8080;
        this->Start();
    }
};
```

### Class member and struct field initialization

All class members and struct fields of simple types (`int`, `bool`, `double`, `enum`, etc.) must be initialized with a default value at declaration. All smart pointers must also be explicitly initialized (`= nullptr`). Complex types with their own default constructors (`std::string`, `std::vector`, `std::map`, etc.) do not need initialization.

```cpp
// Bad: primitives and pointers without initialization
class Worker
{
private:
    bool running;
    int retryCount;
    DevicePtr device;
    std::string name;
};

// Good: primitives and pointers initialized, complex types — not
class Worker
{
private:
    bool running = false;
    int retryCount = 0;
    DevicePtr device = nullptr;
    std::string name;
};
```

### Variable and type naming

Variable and type names must be readable and understandable. Single-letter variables are prohibited, except for loop counters (`i`, `j`, `k`). Words in names are written in full — abbreviations are acceptable only if they are universally accepted and unambiguous.

Acceptable abbreviations: `config` (configuration), `ptr` (pointer), `err` (error), `arg` / `args` (argument/arguments) and similar widely known ones.

Unacceptable abbreviations: `addr` (address), `conn` (connection), `msg` (message), `btn` (button), `srv` (server) and others that are not commonly used in the industry.

```cpp
// Bad: single-letter and unclear abbreviations
auto c = network::CreateConnection();
auto addr = network::ParseAddress(input);
auto msg = messaging::BuildMessage(data);

// Good: full and readable names
auto connection = network::CreateConnection();
auto address = network::ParseAddress(input);
auto message = messaging::BuildMessage(data);

// Good: acceptable abbreviations
auto [config, err] = parser::ParseConfig(path);
auto ptr = std::make_shared<Handler>();

// Good: counter in a loop
for (std::size_t i = 0; i < items.size(); ++i) {
    pipeline::Process(items[i]);
}
```

### Acronyms in names

Acronyms in variable, type, function, and method names are written as regular words — with the first letter capitalized and the rest lowercase. This ensures uniformity of PascalCase / camelCase style and improves readability at word boundaries. The rule applies to acronyms of any length.

```cpp
// Bad: acronym fully uppercase
class HTTPClient
{
public:
    std::tuple<APIResponse, error> SendHTTPRequest(const std::string & url);

private:
    std::string apiURL;
    int userID = 0;
};

// Good: acronym as a regular word
class HttpClient
{
public:
    std::tuple<ApiResponse, error> SendHttpRequest(const std::string & url);

private:
    std::string apiUrl;
    int userId = 0;
};
```

### Explicit call qualification

Code must not contain function calls or variable accesses by bare name if they are not local. Access by bare name is acceptable only for local variables and function parameters. This improves readability and allows immediately understanding where the called function or variable comes from.

- Class members and methods — via `this->`
- Global and namespace functions — via explicit `namespace::Function()`
- Static methods of another class — via `ClassName::Method()`

```cpp
// Bad: unclear where functions come from
error Initialize(DeviceModulePtr module)
{
    auto err = SetupModule(module);
    if (err) {
        return Wrap(err, "Failed to setup");
    }

    Log("Module initialized");
    return nullptr;
}

// Good: explicit qualification of every call
error Initialize(DeviceModulePtr module)
{
    // Setup device module
    auto err = device::SetupModule(module);
    if (err) {
        return errors::Wrap(err, "Failed to setup");
    }

    logging::Log("Module initialized");
    return nullptr;
}
```

## CS-06: Global Scope

### Namespaces, functions, variables, and constants

Avoid creating global functions, variables, and constants. When necessary, all global objects must be placed in a `namespace`. Global variables are declared as `inline` to prevent multiple definition errors at link time. All global names (functions, variables, constants) start with a capital letter.

```cpp
// Bad: global objects without namespace
const int maxRetries = 3;
std::string defaultHost = "localhost";
error Connect(const std::string & address);

// Good: everything in namespace, names capitalized, variables inline
namespace network
{

inline constexpr int MaxRetries = 3;
inline std::string DefaultHost = "localhost";

error Connect(const std::string & address);

}  // namespace network
```

## CS-07: File Structure

### File naming and organization

Each header file (`.hpp`) is named after the main class declared in it. The source file (`.cpp`) with the implementation is named identically. The file name matches the class name in PascalCase.

Header and source files must be separated into different directories.

```
project/
├── include/
│   └── fight/
│       ├── FightClub.hpp
│       └── Fighter.hpp
└── src/
    └── fight/
        ├── FightClub.cpp
        └── Fighter.cpp
```

### Header file structure

A header file must be formatted in a strict sequence. Each structural element is described below.

#### Forward declarations

At the beginning of the namespace, before the class definition, forward declarations are placed for types used in the header. This reduces the number of included headers and speeds up compilation.

#### Type aliases for pointers

After forward declarations, aliases for `std::shared_ptr` to the class are declared. This simplifies usage of the class in the rest of the code.

```cpp
// Forward declarations
class FightClub;

// Type aliases
using FightClubPtr = std::shared_ptr<FightClub>;
```

#### Constants

If the class uses named error constants or other constants, they are placed in a separate nested `namespace` before the class definition.

```cpp
namespace ErrorTypes
{
inline const auto FighterNotFound = errors::New("Fighter not found");
}  // namespace ErrorTypes
```

#### Factory method

A class must provide a static factory method `Create` for creating instances. The factory method is always placed first in the `public` section. Two forms exist:

1. **Fallible** — creation can fail. Returns `std::tuple<Ptr, error>`:

```cpp
static std::tuple<FightClubPtr, error> Create(const Config & config);
```

2. **Infallible** — creation always succeeds. Returns only a pointer (never `nullptr`):

```cpp
static FightClubPtr Create(const Config & config);
```

#### Logical sections with comments

Methods and variables within a class are grouped into logical sections. Each section is separated by a comment of the form `/// [Section Name]`.

#### Comments on methods and variables

Every public and private method, as well as every member variable, must have a documenting comment `/// @brief`.

#### `#pragma region` for collapsing sections

Sections of constructors/destructors and private methods are wrapped in `#pragma region` / `#pragma endregion` for convenient collapsing in the IDE. The region name is formed as `ClassName::SectionName`.

#### Complete element sequence

1. `#pragma once`
2. Header includes (`#include`)
3. `namespace`
4. Forward declarations
5. Type aliases
6. Constants (if needed)
7. Documenting class comment (`/// @brief`)
8. Class definition:
   - `public:`
     - `/// [Fabric Methods]` — factory methods
     - `/// [Section]` — logical groups of public methods
     - `/// [Construction & Destruction]` — constructors/destructor in `#pragma region`
   - `private:`
     - Private methods in `#pragma region`, divided into logical sections
     - `/// [Properties]` — member variables, divided into logical sections

#### Complete header file example

```cpp
// include/fight/FightClub.hpp
#pragma once

#include <errors/errors.hpp>

namespace fight
{

// Forward declarations
class FightClub;

// Type aliases
using FightClubPtr = std::shared_ptr<FightClub>;

/// @brief Errors that can occur in FightClub
namespace ErrorTypes
{
inline const auto FighterNotFound = errors::New("Fighter not found");
}  // namespace ErrorTypes

///
/// @brief
/// FightClub manages fighters and provides operations for adding and searching fighters
///
class FightClub
{
public:
    /// [Fabric Methods]

    /// @brief Creates FightClub instance with given configuration
    static std::tuple<FightClubPtr, error> Create(const std::string & name);

    /// [Fighter Management]

    /// @brief Adds fighter to the club
    error AddFighter(const Fighter & fighter);
    /// @brief Finds fighter by name
    std::tuple<Fighter, error> FindFighter(const std::string & name);

    /// [Construction & Destruction]

#pragma region FightClub::Construct

    /// @brief Copy constructor is deleted
    FightClub(const FightClub &) = delete;
    /// @brief Copy operator is deleted
    FightClub & operator=(const FightClub &) = delete;

    /// @brief Config struct for object construction
    struct Config {
        std::string name;
    };

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit FightClub(const Config & config);
    /// @brief Destructor
    ~FightClub();

#pragma endregion

private:
#pragma region FightClub::PrivateMethods

    /// [Validation]

    /// @brief Checks if fighter with given name already exists
    bool fighterExists(const std::string & name);

#pragma endregion

    /// [Properties]

    /// @brief Club name
    std::string name;
    /// @brief List of fighters in the club
    std::vector<Fighter> fighters;
};

}  // namespace fight
```

```cpp
// src/fight/FightClub.cpp
#include <fight/FightClub.hpp>

namespace fight
{

std::tuple<FightClubPtr, error> FightClub::Create(const std::string & name)
{
    const auto config = Config{.name = name};
    const auto club = std::make_shared<FightClub>(config);
    return {club, nullptr};
}

FightClub::FightClub(const Config & config)
{
    this->name = config.name;
}

FightClub::~FightClub() = default;

error FightClub::AddFighter(const Fighter & fighter)
{
    if (this->fighterExists(fighter.Name())) {
        return errors::Errorf("Fighter '{}' already exists", fighter.Name());
    }

    this->fighters.push_back(fighter);
    return nullptr;
}

std::tuple<Fighter, error> FightClub::FindFighter(const std::string & name)
{
    const auto result = std::ranges::find_if(this->fighters, [&name](const auto & fighter) {
        return fighter.Name() == name;
    });

    if (result == this->fighters.end()) {
        return { {}, ErrorTypes::FighterNotFound};
    }

    return {*result, nullptr};
}

bool FightClub::fighterExists(const std::string & name)
{
    return std::ranges::any_of(this->fighters, [&name](const auto & fighter) {
        return fighter.Name() == name;
    });
}

}  // namespace fight
```
