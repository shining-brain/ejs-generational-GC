print("Start test");

function Container() {
    this.data = [];
}

var old = [];
for (var i = 0; i < 5000; i++) {
  old[i] = new Container();
}

for (var i = 0; i < 50000; i++) { 
    var t = {b: i}; 
}

for (var n = 0; n < 40; n++) {
    for (var i = 0; i < 50; i++) {
        old[i].data = { val: n };
    }
  
    for (var i = 0; i < 50000; i++) { 
        var t = {b: i}; 
}
}

print("Done");











// print("After first GC");
// var old_obj1 = {x: 1};
// var young = {z: 42};
// old_obj1.ref = young;
// print("Assigned to old_obj1");


// for (var i = 0; i < 50000; i++) { var t = {a: i}; }

// var old_obj2 = {y: 2};
// var young2 = {w: 99};
// old_obj2.ref = young2;
// print("Assigned to old_obj2");


// for (var i = 0; i < 50000; i++) { var t = {b: i}; }

// var old_obj3 = {z: 3};
// var young3 = {v: 100};
// old_obj3.ref = young3;
// print("Assigned to old_obj3");


// for (var i = 0; i < 50000; i++) { var t = {c: i}; }

// print("Done");