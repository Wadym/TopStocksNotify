#include <random>
#include <iostream>
#include <chrono>

#include <algorithm>
#include <execution>
#include <memory>
#include <unordered_map>
#include <vector>
#include <deque>

#include <shared_mutex>
#include <condition_variable>


template<typename T>
class threadsafe_queue
{
private:
    //mutable std::mutex mut;
    mutable std::shared_mutex mut;
    std::queue<T> data_queue;
    std::condition_variable_any data_cond;
public:
    threadsafe_queue()
    {}
    threadsafe_queue(const threadsafe_queue& other)
    {
        std::lock_guard<std::shared_mutex> lk(other.mut);
        data_queue = other.data_queue;
    }
    threadsafe_queue& operator=(
        const threadsafe_queue&) = delete;
    void push(T && new_value) {
        std::lock_guard<std::shared_mutex> lk(mut);
        data_queue.push(std::forward<T>(new_value));
        data_cond.notify_one();
    };
    void wait_and_pop(T& value)
    {
        std::unique_lock<std::shared_mutex> lk(mut);
        data_cond.wait(lk, [this] () {return !data_queue.empty(); });
        value = data_queue.front();
        data_queue.pop();
    }
    std::shared_ptr<T> wait_and_pop()
    {
        std::unique_lock<std::shared_mutex> lk(mut);
        data_cond.wait(lk, [this] {return !data_queue.empty(); });
        std::shared_ptr<T> res(std::make_shared<T>(data_queue.front()));
        data_queue.pop();
        return res;
    }
    bool try_pop(T& value)
    {
        std::lock_guard<std::shared_mutex> lk(mut);
        if (data_queue.empty())
            return false;
        value = data_queue.front();
        data_queue.pop();
        return true;
    }
    std::shared_ptr<T> try_pop()
    {
        std::lock_guard<std::shared_mutex> lk(mut);
        if (data_queue.empty())
            return std::shared_ptr<T>();
        std::shared_ptr<T> res(std::make_shared<T>(data_queue.front()));
        data_queue.pop();
        return res;
    }
    bool empty() const
    {
        //std::lock_guard<std::shared_mutex> lk(mut);
        std::shared_lock<std::shared_mutex> lk(mut);
        return data_queue.empty();
    }
};

template<typename Tprice=double>
class SecurityState {
public:
    SecurityState(const int & stock_id, const Tprice& price = 0,
        const unsigned long long& tag = 0 , const Tprice& price_pct_change = 0)
        : _stock_id(stock_id), _price(price), _tag(tag),
        _price_pct_change(price_pct_change) {}
    SecurityState() = default;
    SecurityState<Tprice>(const SecurityState<Tprice>& other)
        : _price(other._price), _stock_id(other._stock_id), _tag(other._tag),
        _price_pct_change(other._price_pct_change) { }
    SecurityState<Tprice>(SecurityState<Tprice>&& other) noexcept
        : _price(std::move(other._price)), _stock_id(std::move(other._stock_id)),
        _tag(std::move(other._tag)), _price_pct_change(std::move(other._price_pct_change)) { };
    SecurityState<Tprice>& operator=(SecurityState<Tprice>&& other) noexcept
    {
        _price = std::move(other._price);
        _stock_id = std::move(other._stock_id);
        _tag = std::move(other._tag);
        _price_pct_change = std::move(other._price_pct_change);
        return *this;
    }
    SecurityState<Tprice>& operator=(const SecurityState<Tprice>& other)
    {
        _price = other._price;
        _stock_id = other._stock_id;
        _tag = other._tag;
        _price_pct_change = other._price_pct_change;
        return *this;
    }
    Tprice get_price() const { return _price; }
    SecurityState<Tprice>& set_price(const Tprice&& price) { _price = std::move(price);
        return *this; }
    SecurityState<Tprice>& set_price(const Tprice& price) { _price = price;
        return *this; }

    Tprice get_price_pct_change() const { return _price_pct_change; }
    SecurityState<Tprice>& set_price_pct_change(const Tprice&& price_pct_change) {
        _price_pct_change = std::move(price_pct_change);
        return *this;
    }
    SecurityState<Tprice>& set_price_pct_change(const Tprice& price_pct_change) {
        _price_pct_change = price_pct_change;
        return *this;
    }

    int get_id() const { return _stock_id; }
    unsigned long long get_tag() const { return _tag; }
    SecurityState<Tprice>& set_tag(const unsigned long long& tag) { _tag = tag;
        return *this; }
    SecurityState<Tprice>& set_tag(const unsigned long long&& tag) {
        _tag = std::move(tag); return *this; }

    SecurityState<Tprice>& set_id(const int& stock_id) {
        _stock_id = stock_id;
        return *this;
    }
    SecurityState<Tprice>& set_id(const int&& stock_id) {
        _stock_id = std::move(stock_id); return *this;
    }
    bool operator < (const SecurityState& rhs) {
        return this->_price_pct_change < rhs._price_pct_change;
    }
    friend bool operator<(Tprice left, const SecurityState& s)
    {
        return left < s._price_pct_change;
    }
    friend bool operator<(const SecurityState& s, Tprice left)
    {
        return s._price_pct_change < left;
    };
    friend std::ostream& operator<<(std::ostream& output, const SecurityState& s) {
        output << "SecurityState : " << s._stock_id << " " << s._price << " "
            << s._price_pct_change << " " << s._tag;
        return output;
    };
private:
    Tprice _price;
    int _stock_id;
    unsigned long long _tag;
    Tprice _price_pct_change;
};

template <size_t N = 20u, typename Tprice = double >
class StockProcessor {
public:
    StockProcessor() : _sc(0, 0, 0) {
        price_largest_pct.reserve(N + 1);
        price_smallest_pct.reserve(N + 1);
        //price_pct.resize(10001, std::move(SecurityState<Tprice>()));
        thread1 = std::thread(&StockProcessor<N, Tprice>::OnLargestChange, this);
        thread2 = std::thread(&StockProcessor<N, Tprice>::OnSmallestChange, this);
    }
    inline void OnQuote(int stock_id, Tprice price) {
        _sc.set_price(price).set_id(stock_id).set_tag(_sc.get_tag() + 1);
        OnStockInit();
        CalcDeltaPct();
        if (!IsInSmallestN()) {
            IsInLargestN();
        }
    }
    void ShutdownProcessingThreads() {
        done = true;
        price_largest_pct_queue.push(std::move(SecurityState<Tprice>()));
        price_smallest_pct_queue.push(std::move(SecurityState<Tprice>()));
    }
    std::thread thread1;
    std::thread thread2;
private:
    inline void CalcDeltaPct() {
        try {
            _sc.set_price_pct_change(std::move<Tprice>(100 * (_sc.get_price() -
                price_base[_sc.get_id()]) / price_base[_sc.get_id()]) );
        }
        catch (std::logic_error e) {
            std::cerr << __func__ << " : " << e.what() << " : error, probably SecurityState is not available any more";
        }
        catch (...) {
            std::cerr << __func__ << ": Unexpected error";
        }
    }
    inline void OnStockInit()//const int& stock_id, const Tprice& price)
    {
        if (price_base.find(_sc.get_id()) == price_base.end()) {
            price_base[_sc.get_id()] = _sc.get_price();
            price_largest_pct_min = -std::numeric_limits<Tprice>::max();
            price_smallest_pct_max = std::numeric_limits<Tprice>::max();
            return;
        }
        return;
    }
    inline bool IsInLargestN()
    {
        auto it = std::find_if(price_largest_pct.begin(), price_largest_pct.end(),
            [&](const SecurityState<Tprice> e) {return (e.get_id() == _sc.get_id()); });
        if (it == price_largest_pct.end())
        {
            price_largest_pct.emplace_back(_sc.get_id(), _sc.get_price(), _sc.get_tag(), _sc.get_price_pct_change() );
        }
        else
        {
            price_largest_pct[std::distance(price_largest_pct.begin(), it)]
                .set_price(_sc.get_price())
                .set_price_pct_change(_sc.get_price_pct_change())
                .set_tag(_sc.get_tag());
        }
        std::sort(price_largest_pct.begin(), price_largest_pct.end(),
            [](const SecurityState<Tprice>& l, const SecurityState<Tprice>& r) {
                return l.get_price_pct_change() > r.get_price_pct_change();
            });
        if (price_largest_pct.size() > N) {
            price_largest_pct.pop_back();
        }
        auto itsorted = std::find_if(price_largest_pct.begin(), price_largest_pct.end(),
            [&](const SecurityState<Tprice> e) {return (e.get_tag() == _sc.get_tag()); });
        if (itsorted != price_largest_pct.end())
        {
            price_largest_pct_queue.push(std::move(_sc)); //sc is not available any more
            return true;
        }
        return false;
    }
    inline bool IsInSmallestN()
    {
        auto it = std::find_if(price_smallest_pct.begin(), price_smallest_pct.end(),
            [&](const SecurityState<Tprice> e) {return (e.get_id() == _sc.get_id()); });
        if (it == price_smallest_pct.end())
        {
            price_smallest_pct.emplace_back(_sc.get_id(), _sc.get_price(),
                _sc.get_tag(), _sc.get_price_pct_change());
        }
        else
        {
            price_smallest_pct[std::distance(price_smallest_pct.begin(), it)]
                .set_price(_sc.get_price())
                .set_price_pct_change(_sc.get_price_pct_change())
                .set_tag(_sc.get_tag());
        }
        std::sort(price_smallest_pct.begin(), price_smallest_pct.end(),
            [](const SecurityState<Tprice>& l, const SecurityState<Tprice>& r) {
                return l.get_price_pct_change() > r.get_price_pct_change();
            });
        if (price_smallest_pct.size() > N) {
            price_smallest_pct.pop_back();
        }
        auto itsorted = std::find_if(price_smallest_pct.begin(),
            price_smallest_pct.end(),
            [&](const SecurityState<Tprice> e) {return (e.get_tag() == _sc.get_tag()); });
        if (itsorted != price_smallest_pct.end())
        {
            price_smallest_pct_queue.push(std::move(_sc)); //sc is not available any more
            return true;
        }
        return false;
    }
    void OnLargestChange(){
        while (_allowEventProcessing)
        {
            SecurityState<Tprice> data;
            price_largest_pct_queue.wait_and_pop(data);
            //Process data
            // need protection too to implement
            std::cout << "Gainer: " << data << std::endl;
            
            //exit on outside signal received
            if (done)
            {
                if (price_largest_pct_queue.empty()) {
                    break;
                }
            }
        }
        std::cout << "OnLargestChange Done" << std::endl;
    }
    void OnSmallestChange() {
        while (_allowEventProcessing)
        {
            SecurityState<Tprice> data;
            price_smallest_pct_queue.wait_and_pop(data);
            //Process data
            // need protection too to implement
            std::cout << "Loser: " << data << std::endl;

            if (done)
            {
                //std::this_thread::sleep_for(std::chrono::milliseconds(300));
                if (price_smallest_pct_queue.empty()) {
                    break;
                }
            }
        }
        std::cout << "OnSmallestChange Done" << std::endl;
    }
private:
    volatile bool _allowEventProcessing = true;
    volatile bool done = false;
    SecurityState<Tprice> _sc;
    std::unordered_map<int, Tprice> price_base;
    threadsafe_queue<SecurityState<>> price_largest_pct_queue;
    threadsafe_queue<SecurityState<>> price_smallest_pct_queue;

    Tprice price_largest_pct_min = -std::numeric_limits<Tprice>::max();
    Tprice price_smallest_pct_max = std::numeric_limits<Tprice>::max();
    std::vector <SecurityState<Tprice> > price_largest_pct;
    std::vector <SecurityState<Tprice> > price_smallest_pct;
};
int main(int argc, char* argv[])
{
    const int stock_data_frames_to_simulate = 100;
    const int stock_number_to_simulate = 10000;
    StockProcessor<10> sp;
    
    std::chrono::microseconds duration{ 0 };

    for(long i = 0; i < stock_data_frames_to_simulate; ++i)
    {
        std::random_device myRandomDevice;
        unsigned seed = myRandomDevice();
        std::default_random_engine myRandomEngine(seed);
        //std::uniform_int_distribution<int> myUnifIntDist(100, 350);
        std::uniform_real_distribution<double> myUnifIntDist(100, 350);
        for (int j = 1; j <= stock_number_to_simulate; j++) {
            double number = static_cast<double>(myUnifIntDist(myRandomEngine));
            auto start = std::chrono::high_resolution_clock::now();

            sp.OnQuote(j, number);

            auto stop = std::chrono::high_resolution_clock::now();
            duration += std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
        }
    }

    sp.ShutdownProcessingThreads();
    sp.thread1.join();
    sp.thread2.join();

    std::cout << "duration, microseconds : " << duration.count() << std::endl;
    std::cout << "duration per SecurityState, microseconds : " << duration.count() / ((double)((stock_data_frames_to_simulate) * (stock_number_to_simulate))) << std::endl;

    return 0;
}