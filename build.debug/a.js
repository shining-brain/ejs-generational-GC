// function objectTest() {
//     var a = 10;
// }

// var count = 0;
// print("=== Loop start ===");
// for (var i = 0; i < 5000000; i++) {
//     var b = new objectTest();
//     count++;
//     if (i % 100000 == 0) {
//         print("i = " + i + ", count = " + count); 
//     }
// }

// print("=== Loop end ===");
// print("Final i = " + i);
// print("Final count = " + count);
// print("=== Program end ===");




function objectTest_binary_tree() {
    var left = null;
    var right = null;
    this.setLeft = function(l) {
        left = l;
    }
    this.setRight = function(r) {
        right = r;
    }
    this.getLeft = function() {
        return left;
    }
    this.getRight = function() {
        return right;
    }
}

var count = 0;
// var root = null;
print("=== Loop start ===");

for (var i = 0; i < 50000; i++) {
    var root = new objectTest_binary_tree();
    var leftChild = new objectTest_binary_tree();
    var rightChild = new objectTest_binary_tree();
    root.setLeft(leftChild);
    root.setRight(rightChild);
    count++;
    // root = leftChild; // go down the left side
    if (i % 100 == 0) {
        print("i = " + i + ", count = " + count); 
    }
}

print("=== Loop end ===");
print("Final i = " + i);
print("Final count = " + count);
print("=== Program end ===");



