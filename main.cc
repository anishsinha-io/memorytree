#include <iostream>
#include <mutex>
#include <optional>
#include <stack>
#include <vector>

template <typename T, typename K>
class Node {
    class SplitResult {
       public:
        inline auto GetLeft() -> Node * {
            return left_;
        }

        inline auto GetRight() -> Node * {
            return right_;
        }

        inline auto HasRoot() -> bool {
            return new_root_ != nullptr;
        }

        inline auto GetRoot() -> Node * {
            return new_root_;
        }

       private:
        Node *left_;
        Node *right_;
        Node *new_root_;
        T promoted_key;
    };

   public:
    explicit Node(int min_order) : min_order_(min_order), leaf_(true), root_(false), right_link_(nullptr), out_link_(nullptr) {
    }
    ~Node() = default;

    inline void SetKeys(std::vector<T *> keys) {
        keys_ = keys;
    }

    inline void SetChildren(std::vector<K *> children) {
        children_ = children;
    }

    friend auto operator<<(std::ostream &os, const Node &node) -> std::ostream & {
        os << "Node {\n\tleaf_: " << node.leaf_ << ",\n\troot_: " << node.root_ << ",\n\tmin_order_: " << node.min_order_ << ",\n}";
        return os;
    }

    friend auto operator<<(std::ostream &os, const Node *node) -> std::ostream & {
        os << "Node {\n\tleaf_: " << node->leaf_ << ",\n\troot_: " << node->root_ << ",\n\tmin_order_: " << node->min_order_ << ",\n}";
        return os;
    }

   private:
    template <typename R>
    static auto SplitVec(std::vector<R> &vec) -> std::tuple<std::vector<R>, std::vector<R>> {
        auto mid = vec.size() % 2 == 0 ? vec.size() / 2 : vec.size() / 2 + 1;
        std::vector<R> low(vec.begin(), vec.begin() + mid);
        std::vector<R> high(vec.begin() + mid, vec.end());
        return std::make_tuple(low, high);
    }

    bool leaf_;
    bool root_;
    int min_order_;
    std::vector<T *> keys_;
    std::vector<K *> children_;
    std::mutex latch_;
    Node *right_link_, *out_link_;
};

auto main(int argc, char **argv) -> int {
    auto node = new Node<void *, void *>(2);
    std::cout << node << std::endl;
}