from Interface import *

def MainActivity():
    global app
    app.processEvents()
      
if __name__ == '__main__':            
    app = QtWidgets.QApplication(sys.argv)
    MainWindow = MyApp()
    MainWindow.show()
    MainWindow.ScanButtonClicked()
    timer = pg.QtCore.QTimer()
    timer.timeout.connect(MainActivity)
    timer.start(20)
    app.exec()
