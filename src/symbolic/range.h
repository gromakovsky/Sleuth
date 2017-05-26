struct sym_range
{
    sym_expr lo;
    sym_expr hi;

    sym_range & operator|=(sym_range const &);  // union
    sym_range & operator&=(sym_range const &);  // intersection

    sym_range & operator+=(sym_range const &);
    sym_range & operator-=(sym_range const &);
    sym_range & operator*=(sym_expr const &);
    sym_range & operator*=(sym_range const &);
    sym_range & operator/=(sym_expr const &);
    sym_range & operator/=(sym_range const &);

    static sym_range full;
    static sym_range empty;
};

sym_range operator|(sym_range const & a, sym_range const & b);
sym_range operator&(sym_range const & a, sym_range const & b);

sym_range operator+(sym_range const & a, sym_range const & b);
sym_range operator-(sym_range const & a, sym_range const & b);
sym_range operator*(sym_range const & a, sym_expr const & b);
sym_range operator*(sym_expr const & a, sym_range const & b);
sym_range operator*(sym_range const & a, sym_range const & b);
sym_range operator/(sym_range const & a, sym_expr const & b);
sym_range operator/(sym_range const & a, sym_range const & b);

bool operator==(sym_range const & a, sym_range const & b);
bool operator!=(sym_range const & a, sym_range const & b);

llvm::raw_ostream & operator<<(llvm::raw_ostream &, sym_range const &);

sym_range const_sym_range(scalar_t);
sym_range var_sym_range(var_id const &);

using scalar_range = std::pair<scalar_t, scalar_t>;

boost::optional<scalar_range> to_scalar_range(sym_range const &);
