
import struct
import serial
from serial import *
import sys
import glob
import time

import threading
import time


from tkinter import *

from PIL import Image, ImageTk, ImageColor
from tkinter.ttk import Combobox
from math import sin


WIDTH = 600
HEIGHT = 800
QRCODEX = 200
QRCODEY =  200
QRCODESZ = 200


availablePorts = []
serialPortHandle = None
selectedPort = None
qrcoderequest = b"\x0A\x00%s\00"

loadedImage = None

def scanForAvailablePorts():
	global availablePorts
	print("Status:Select a port.....0 - 255")
	ports = ['COM%s' % (i + 1) for i in range(256)]
	i = 0
	result = []
	for port in ports:
		try:
			s = serial.Serial(port)
			result.append(port)
			s.close()
	
		except (Exception):
			pass
			i = i+1
			
	
	for i in range(len(result)):
		print("\t\t("+str(i)+")\t"+result[i])
		
	availablePorts = result

	
def printQRCODE(size, payload):
	global img
	global canvas
	global window
	global loadedImage
	scale = 1
	im = Image.new('1', (size,size)) # create the Image of size 1 pixel 
	
	for x in range(0,size):
		for  y in range(0, size):
			offset = y * size + x
			if (payload[offset >> 3] & (1 << (7 - (offset & 0x07)))) != 0:
				print("x",end='')
				#if scale ==1:
				#	img.put("#000000", (QRCODEX+x,QRCODEY+y))

				im.putpixel((x,y), ImageColor.getcolor('black', '1')) # or whatever color you wish
		
			else:
				print(" ",end='')
				#if scale == 1:
				#	img.put("#FFFFFF", (QRCODEX+x,QRCODEY+y))

				im.putpixel((x,y), ImageColor.getcolor('white', '1')) # or whatever color you wish
		print("")

	
	im = im.resize((300, 300), Image.ANTIALIAS) 
	im.save('simpleQRcode.png') 	# or any image format
	
	
	loadedImage = PhotoImage(file="simpleQRcode.png")
	#pimg = PhotoImage(im)
	nameLabel = Label(window, image=loadedImage)
	nameLabel.place(relx=.5, rely=.5, anchor="center")
	#nameLabel.pack()

	#canvas.create_image((200/2, 200/2), image= pimg, state="normal", anchor =NW )
	#canvas.pack()

	
def handlerGenerateQR():
	data = entryData.get()
	length = len(data) + 4
	frame = []
	frame.append(bytes([length]))
	frame.append(qrcoderequest%(str.encode(data)))
		
	print(frame)
	
	for a in frame:
		serialPortHandle.write(bytes(a))
		time.sleep(0.001)

	
	print("Status:Sent, waiting for response")
	
	timeoutstart = time.time()*1000
	
	waiting = True 
	while waiting:
		if serialPortHandle.in_waiting >=4:
			length = int(ord(serialPortHandle.read(1)))
			command = serialPortHandle.read(2)
			size = ord(serialPortHandle.read(1))
			
			length = length -4 
			while waiting and length != serialPortHandle.in_waiting:
				if time.time()*1000-timeoutstart>3000: # 3 seconds
					waiting = False

			if waiting:
				payload = serialPortHandle.read(length)  # plus checksum
				printQRCODE(size,payload)
				waiting = False
				
		elif time.time()*1000-timeoutstart>3000: # 3 seconds
			waiting = False		
			
	
	print("time waited:%s ms"%str(time.time()*1000-timeoutstart))

def portSelected(event):
	global selectedPort
	selectedPort=cb.get()
	print(selectedPort)

def handlerRefreshComPorts():
	print("handlerRefreshComPorts")
	scanForAvailablePorts()
	cb.configure(values=availablePorts)			
	
def handlerConnection():
	global serialPortHandle
	print("handlerConnection")
	try:
	
	
		if serialPortHandle!= None and serialPortHandle.isOpen():
			print("Status: trying to close port %s"%serialPortHandle.name)
			serialPortHandle.close()
			print("Status: port has been closed!")
			buttonConnect["text"] = "Open"
		else:
			print("Status: trying to open port %s"%(selectedPort))
			serialPortHandle = serial.Serial(selectedPort, 115200, timeout=1)
			print("Status: port is open!")
			buttonConnect["text"] = "Close Port"


	except Exception as e:
		print(e)

	
scanForAvailablePorts()




	

#t = threading.Thread ( target=read_incoming_packets,args=() )
#t.start()


window=Tk()
window.title('QR Generator')

window.geometry("500x500") #You want the size of the app to be 500x500
window.resizable(0, 0) #Don't allow resizing in the x or y direction


#canvas = Canvas(window, width=WIDTH, height=HEIGHT, bg="#FFFFFF")
#canvas.pack()


buttonRefresh = Button(window, text='Refresh', command=handlerRefreshComPorts)
buttonRefresh.place(x=5,y=5)

buttonConnect = Button(window, text='Open Port', command=handlerConnection)
buttonConnect.place(x=55,y=5)



cb=Combobox(window)
cb.place(x=5, y=40)
cb.configure(values=availablePorts)
cb.bind("<<ComboboxSelected>>", portSelected)
cb.current(0)
selectedPort = availablePorts[0]


buttonGenerate = Button(window, text='Generate QR', command=handlerGenerateQR)
buttonGenerate.place(x=120,y=5)

labelData = Label(window, text="Data:")
labelData.place(x=220,y=5)

entryData = Entry(window,width = 40, text="Hello QR")
entryData.place(x=250,y=7)
window.mainloop()

			
