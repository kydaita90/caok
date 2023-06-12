contract A {
    uint x;
    constructor() {
        x = 42;
    }
    function f() public returns(uint256) {
        return x;
    }
}
contract B is A {
    uint public y = f();
}
// ====
// compileViaYul: true
// ----
// constructor() ->
// gas irOptimized: 119739
// y() -> 42
