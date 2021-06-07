// Copyright 2021 milerol <milerol181@yandex.ru>

#ifndef INCLUDE_HEADER_HPP_
#define INCLUDE_HEADER_HPP_

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <list>

class Log {
 public:
  Log(Log& other) = delete;
  void operator=(const Log& other) = delete;

  void Write(std::string_view message) const { *out_ << message << std::endl; }

  void WriteDebug(std::string_view message) const {
    if (level_ > 0) *out_<< message << std::endl;
  }

  static Log* singleton_;

  static Log* GetInstance(size_t level) {
    if (singleton_ == nullptr) {
      singleton_ = new Log(level);
    }
    return singleton_;
  }

 protected:

  explicit Log(size_t level) : level_(level) { out_ = &std::cout; }

 private:
  size_t level_ = 0;
  mutable std::ostream* out_;
};

Log* Log::singleton_ = nullptr;

struct Item {
  std::string id;
  std::string name;
  float score = 0;
};

constexpr size_t kMinLines = 10;

class Observer1 {
 public:
  virtual void OnDataLoad(const std::vector<Item>& old_items,
                          const std::vector<Item>& new_items) = 0;
  virtual void OnRawDataLoad(const std::vector<std::string>& old_items,
                             const std::vector<std::string>& new_items) = 0;
};

class Observer2 {
 public:
  virtual void OnLoaded(const std::vector<Item>& new_items) = 0;
  virtual void Skip(const Item& item) = 0;
};

class Parent_observers {
 public:
  virtual void Attach(Observer1* observer) = 0;
  virtual void Detach(Observer1* observer) = 0;

  virtual void Attach(Observer2* observer) = 0;
  virtual void Detach(Observer2* observer) = 0;
};


class UsedMemory:public Observer1 {
 public:
  explicit UsedMemory(const Log* log) : log_(log) {}

  void clear()
  {
    used_ = 0;
  }

  void OnDataLoad(const std::vector<Item>& old_items,
                  const std::vector<Item>& new_items) {
    log_->WriteDebug("UsedMemory::OnDataLoad");
    for (const auto& item : old_items) {
      used_ -= item.id.capacity();
      used_ -= item.name.capacity();
      used_ -= sizeof(item.score);
    }

    for (const auto& item : new_items) {
      used_ += item.id.capacity();
      used_ += item.name.capacity();
      used_ += sizeof(item.score);
    }
    log_->Write("UsedMemory::OnDataLoad: new size = " +
                std::to_string(used_));
  }

  void OnRawDataLoad(const std::vector<std::string>& old_items,
                     const std::vector<std::string>& new_items) {
    log_->WriteDebug("UsedMemory::OnRawDataLoads");
    for (const auto& item : old_items) {
      used_ -= item.capacity();
    }

    for (const auto& item : new_items) {
      used_ += item.capacity();
    }
    log_->Write("UsedMemory::OnDataLoad: new size = " +
                std::to_string(used_));
  }

  [[nodiscard]]size_t used() const { return used_; }

 private:
  const Log* log_;
  size_t used_ = 0;
};

class StatSender:public Observer2 {
 public:
  explicit StatSender(const Log& log) : log_(&log) {}
  void OnLoaded(const std::vector<Item>& new_items) {
    log_->WriteDebug("StatSender::OnDataLoad");

    AsyncSend(new_items, "/items/loaded");
  }

  void Skip(const Item& item) { AsyncSend({item}, "/items/skiped"); }

 private:
  void AsyncSend(const std::vector<Item>& items, std::string_view path)
  {
    log_->Write(path);
    log_->Write("send stat " + std::to_string(items.size()));

    for (const auto& item : items) {
      log_->WriteDebug("send: " + item.id);
      // ... some code
      fstr << item.id << item.name << item.score;
      fstr.flush();
    }
  }

  const Log* log_;
  std::ofstream fstr{"network", std::ios::binary};
};


class PageContainer:public Parent_observers {
 public:
  explicit PageContainer() {}

  void Load(std::istream& io, float threshold) {
    std::vector<std::string> raw_data;

    while (!io.eof()) {
      std::string line;
      std::getline(io, line, '\n');
      raw_data.push_back(std::move(line));
    }

    if (raw_data.size() < kMinLines) {
      throw std::runtime_error("too small input stream");
    }

    for (auto & list : list_observer1)
      list -> OnRawDataLoad(raw_data_, raw_data);
    raw_data_ = std::move(raw_data);

    std::vector<Item> data;
    std::set<std::string> ids;
    for (const auto& line : raw_data_) {
      std::stringstream stream(line);

      Item item;
      stream >> item.id >> item.name >> item.score;

      if (auto&& [_, inserted] = ids.insert(item.id); !inserted) {
        throw std::runtime_error("already seen");
      }

      if (item.score > threshold) {
        data.push_back(std::move(item));
      } else {
        for (auto & list : list_observer2)
          list -> Skip(item);
      }
    }

    if (data.size() < kMinLines) {
      throw std::runtime_error("oops");
    }

    for (auto & list : list_observer1)
      list -> OnDataLoad(data_, data);

    for (auto & list : list_observer2)
      list -> OnLoaded(data);
    data_ = std::move(data);
  }

  const Item& ByIndex(size_t i) const { return data_[i]; }

  const Item& ById(const std::string& id) const {
    auto it = std::find_if(std::begin(data_), std::end(data_),
                           [&id](const auto& i) { return id == i.id; });
    return *it;
  }

  void Reload(float threshold) {
    std::vector<Item> data;
    std::set<std::string> ids;
    for (const auto& line : raw_data_) {
      std::stringstream stream(line);

      Item item;
      stream >> item.id >> item.name >> item.score;

      if (auto&& [_, inserted] = ids.insert(item.id); !inserted) {
        throw std::runtime_error("already seen");
      }

      if (item.score > threshold) {
        data.push_back(std::move(item));
      } else {
        for (auto & list : list_observer2)
          list -> Skip(item);
      }
    }

    if (data.size() < kMinLines) {
      throw std::runtime_error("oops");
    }

    for (auto & list : list_observer1)
      list->OnDataLoad(data_, data);
    for (auto & list : list_observer2)
      list -> OnLoaded(data);
    data_ = std::move(data);
  }


  void Attach(Observer1* observer) override {
    list_observer1.push_back(observer);
  }

  void Detach(Observer1* observer) override {
    list_observer1.remove(observer);
  }

  void Attach(Observer2* observer) override {
    list_observer2.push_back(observer);
  }

  void Detach(Observer2* observer) override {
    list_observer2.remove(observer);
  }

  PageContainer(const Log* log) : log_(log) {}

  [[nodiscard]] size_t data_size() const { return data_.size(); }

 private:
  const Log* log_;
  std::list<Observer1*> list_observer1;
  std::list<Observer2*> list_observer2;
  std::vector<Item> data_;
  std::vector<std::string> raw_data_;
};

class Histogram
{
 public:
  Histogram() = default;

  void on_data_load(const std::vector<Item> &,
                    const std::vector<Item> & new_items)
  {
    for (const auto& new_item : new_items)
    {
      score_mean += static_cast<float>(new_item.score);
    }
    score_mean /= static_cast<float>(new_items.size());

    Log::GetInstance(0)->Write("Average: " +
                               std::to_string(score_mean) +
                               " Number of discarded: " +
                               std::to_string(discarded_items));
  }

  [[nodiscard]] float average() const { return score_mean; }
  [[nodiscard]] size_t thrown() const { return discarded_items; }

 private:
  float score_mean = 0;
  size_t  discarded_items = 0;
};

#endif // INCLUDE_HEADER_HPP_
