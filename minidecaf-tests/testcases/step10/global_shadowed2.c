int a = 3;

int main() {
    int reta = 0;
    int retb = 0;
    if (a) {
        int a = 0;
        if(a == 0){
          int a = 4;
          retb = 1;
        }
        reta = 4;
    }
    return reta + retb + a;
}