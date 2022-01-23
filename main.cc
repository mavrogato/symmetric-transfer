
#include <type_traits>
#include <memory>
#include <cassert>

namespace std::inline experimental
{
#if    !__has_builtin(__builtin_coro_resume)          \
    || !__has_builtin(__builtin_coro_destroy)         \
    || !__has_builtin(__builtin_coro_done)            \
    || !__has_builtin(__builtin_coro_promise)         \
    || !__has_builtin(__builtin_coro_noop)
# error "clang coroutine builtin supports required"
#endif

/////////////////////////////////////////////////////////////////////////////
template <class T, class = std::void_t<>> struct coroutine_traits_sfinae { };
template <class T>
struct coroutine_traits_sfinae<T, typename std::void_t<typename T::promise_type>> {
    using promise_type = typename T::promise_type;
};

/////////////////////////////////////////////////////////////////////////////
template <class Ret, class... Args>
struct coroutine_traits : public coroutine_traits_sfinae<Ret> { };

/////////////////////////////////////////////////////////////////////////////
template <class Promise = void>
class coroutine_handle;
template <>
class coroutine_handle<void> {
public:
    constexpr coroutine_handle() noexcept : handle_(nullptr) { }
    constexpr coroutine_handle(nullptr_t) noexcept : handle_(nullptr) { }

    auto& operator = (nullptr_t) noexcept {
        this->handle_ = nullptr;
        return *this;
    }

    constexpr void* address() const noexcept { return this->handle_; }
    constexpr explicit operator bool() const noexcept { return this->handle_; }

    void operator () () { resume(); }

    void resume() {
        assert(is_suspended());
        assert(!done());
        __builtin_coro_resume(this->handle_);
    }
    void destroy() {
        assert(is_suspended());
        __builtin_coro_destroy(this->handle_);
    }
    bool done() const {
        assert(is_suspended());
        return __builtin_coro_done(this->handle_);
    }

public:
    friend bool operator == (coroutine_handle lhs, coroutine_handle rhs) noexcept {
        return lhs.address() == rhs.address();
    }
    friend bool operator < (coroutine_handle lhs, coroutine_handle rhs) noexcept {
        return less<void*>()(lhs.address(), rhs.address());
    }

private:
    bool is_suspended() const noexcept {
        return this->handle_;
    }

private:
    void* handle_;

    template <class Promise> friend class coroutine_handle;
};

template <class Promise>
class coroutine_handle : public coroutine_handle<> {
    using Base = coroutine_handle<>;

public:
    coroutine_handle() noexcept : Base() { }
    coroutine_handle(nullptr_t) noexcept : Base(nullptr) { }

    coroutine_handle& operator = (nullptr_t) noexcept {
        Base::operator = (nullptr);
        return *this;
    }

    Promise& promise() const {
        return *static_cast<Promise*>(
            __builtin_coro_promise(this->handle_, alignof (Promise), false));
    }

public:
    static coroutine_handle from_address(void* addr) noexcept {
        coroutine_handle tmp;
        tmp.handle_ = addr;
        return tmp;
    }
    static coroutine_handle from_address(nullptr_t) noexcept {
        // NOTE: this overload isn't required by the standard but is needed so
        // the deleted Promise* overload doesn't make from_address(nullptr) 
        // ambiguous.
        //  Should from address work with nullptr?
        return coroutine_handle(nullptr);
    }
    template <class T, bool CALL_IS_VALID = false>
    static coroutine_handle from_address(T*) noexcept {
        static_assert(CALL_IS_VALID);
    }
    template <bool CALL_IS_VALID = false>
    static coroutine_handle from_address(Promise*) noexcept {
        static_assert(CALL_IS_VALID);
    }
    static coroutine_handle from_promise(Promise& promise) noexcept {
        using RawPromise = typename std::remove_cv<Promise>::type;
        coroutine_handle tmp;
        tmp.handle_ = __builtin_coro_promise(
            std::addressof(const_cast<RawPromise&>(promise)),
            alignof (Promise), true);
        return tmp;
    }
};

struct noop_coroutine_promise { };
template <>
class coroutine_handle<noop_coroutine_promise>
    : public coroutine_handle<>
{
    using Base = coroutine_handle<>;
    using Promise = noop_coroutine_promise;

public:
    Promise& promise() const {
        return *static_cast<Promise*>(
            __builtin_coro_promise(this->handle_, alignof (Promise), false));
    }

    constexpr explicit operator bool() const noexcept { return true; }
    constexpr bool done() const noexcept { return false; }

    constexpr void operator()() const noexcept { }
    constexpr void resume() const noexcept { }
    constexpr void destroy() const noexcept { }

private:
    friend coroutine_handle<noop_coroutine_promise> noop_coroutine() noexcept;

    coroutine_handle() noexcept {
        this->handle_ = __builtin_coro_noop();
    }
};

struct suspend_never {
    bool await_ready() const noexcept { return true; }
    void await_suspend(coroutine_handle<>) const noexcept { }
    void await_resume() const noexcept { }
};

struct suspend_always {
    bool await_ready() const noexcept { return false; }
    void await_suspend(coroutine_handle<>) const noexcept { }
    void await_resume() const noexcept { }
};

}// end of namespace std::inline experimental

namespace std
{

template <class T>
struct hash<experimental::coroutine_handle<T>> {
    using arg_type = experimental::coroutine_handle<T>;
    std::size_t operator() (arg_type const& v) const noexcept {
        return hash<void*>()(v.address());
    }
};

template <class T>
struct generator {
    struct promise_type {
        T value_;

        generator get_return_object() noexcept { return generator{*this}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void unhandled_exception() { throw; }
        std::suspend_always yield_value(T const& value) noexcept {
            this->value_ = value;
            return {};
        }
        void return_void() noexcept { }
    };
    struct iterator {
        using iterator_category = std::input_iterator_tag;
        using size_type         = std::size_t;
        using difference_type   = std::ptrdiff_t;
        using value_type        = std::remove_cvref_t<T>;
        using reference         = value_type&;
        using const_reference   = value_type const&;
        using pointer           = value_type*;
        using const_pointer     = value_type const*;

        std::coroutine_handle<promise_type> coro_ = nullptr;

        iterator() = default;
        explicit iterator(std::coroutine_handle<promise_type> coro) noexcept : coro_(coro) { }
        iterator& operator++() {
            if (this->coro_.done()) {
                this->coro_ = nullptr;
            }
            else {
                this->coro_.resume();
            }
            return *this;
        }
        iterator& operator++(int) {
            auto tmp = *this;
            ++*this;
            return tmp;
        }
        [[nodiscard]]
        friend bool operator==(iterator const& lhs, std::default_sentinel_t) noexcept {
            return lhs.coro_.done();
        }
        [[nodiscard]]
        friend bool operator!=(std::default_sentinel_t, iterator const& rhs) noexcept {
            return rhs.coro_.done();
        }
        [[nodiscard]] const_reference operator*() const noexcept {
            return this->coro_.promise().value_;
        }
        [[nodiscard]] reference operator*() noexcept {
            return this->coro_.promise().value_;
        }
        [[nodiscard]] const_pointer operator->() const noexcept {
            return std::addressof(this->coro_.promise().value_);
        }
        [[nodiscard]] pointer operator->() noexcept {
            return std::addressof(this->coro_.promise().value_);
        }
    };
    [[nodiscard]] iterator begin() {
        if (this->coro_) {
            if (this->coro_.done()) {
                return {};
            }
            else {
                this->coro_.resume();
            }
        }
        return iterator{this->coro_};
    }
    [[nodiscard]] std::default_sentinel_t end() noexcept { return std::default_sentinel; }

    [[nodiscard]] bool empty() noexcept { return this->coro_.done(); }

    explicit generator(promise_type& prom) noexcept
        : coro_(std::coroutine_handle<promise_type>::from_promise(prom))
    {
    }
    generator() = default;
    generator(generator&& rhs) noexcept
        : coro_(std::exchange(rhs.coro_, nullptr))
    {
    }
    ~generator() noexcept {
        if (this->coro_) {
            this->coro_.destroy();
        }
    }
    generator& operator=(generator const&) = delete;
    generator& operator=(generator&& rhs) {
        if (this!= &rhs) {
            this->coro_ = std::exchange(rhs.coro_, nullptr);
        }
        return *this;
    }

private:
    std::coroutine_handle<promise_type> coro_ = nullptr;
};

/////////////////////////////////////////////////////////////////////////////
class task {
public:
    class promise_type {
    public:
        task get_return_object() noexcept {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { throw; }

        struct final_awaiter {
            bool await_ready() noexcept {
                return false;
            }
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                // The corutine is now suspended at the final-suspend point.
                // Lookup its continuation in the promise and resume it.
                h.promise().continuation.resume();
            }
            void await_resume() noexcept {}
        };

        final_awaiter final_suspend() noexcept { return {}; }

        std::coroutine_handle<> continuation;
    };

    task(task&& t) noexcept : coro_(std::exchange(t.coro_, {})) { }
    ~task() { if (coro_) coro_.destroy(); }

    class awaiter {
        friend class task;

    public:
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> continuation) noexcept {
            // Store the continuation in the task's promise so that the final_suspend()
            // knows to resume this coroutine when the task completes.
            coro_.promise().continuation = continuation;

            // Then we resume the task's coroutine, which is currently suspend
            // at the initial-suspend-point (ie. at the open curly brace).
            coro_.resume();
        }
        void await_resume() noexcept {}

    private:
        explicit awaiter(std::coroutine_handle<task::promise_type> h) noexcept
            : coro_(h)
        {
        }

        std::coroutine_handle<task::promise_type> coro_;
    };

    awaiter operator co_await() && noexcept {
        return awaiter{coro_};
    }

private:
    explicit task(std::coroutine_handle<promise_type> h) noexcept : coro_(h) { }

    std::coroutine_handle<promise_type> coro_;
};

} // end of namespace std

#include <iostream>

int main() {
    static constexpr auto iota = [](int n) -> std::generator<int> {
        for (int i = 0; i < n; ++i) {
            co_yield i;
        }
    };
    for (auto item : iota(10)) {
        std::cout << item << std::endl;
    }
    return 0;
}
