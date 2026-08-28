package java.util;

import java.io.Serializable;

public class Vector<E> extends AbstractList<E> implements List<E>, RandomAccess, Cloneable, Serializable {
    private static final long serialVersionUID = -2767605614048989439L;
    protected Object[] elementData;
    protected int elementCount;
    protected int capacityIncrement;

    public Vector(int initialCapacity, int capacityIncrement) {
        if (initialCapacity < 0) throw new IllegalArgumentException("Illegal Capacity: "+ initialCapacity);
        this.elementData = new Object[initialCapacity];
        this.capacityIncrement = capacityIncrement;
    }
    public Vector(int initialCapacity) { this(initialCapacity, 0); }
    public Vector() { this(10); }
    public Vector(Collection<? extends E> c) {
        elementData = c.toArray();
        elementCount = elementData.length;
    }

    public synchronized void copyInto(Object[] anArray) {
        System.arraycopy(elementData, 0, anArray, 0, elementCount);
    }
    public synchronized void trimToSize() {
        if (elementCount < elementData.length) {
            Object[] oldData = elementData;
            elementData = new Object[elementCount];
            System.arraycopy(oldData, 0, elementData, 0, elementCount);
        }
    }
    public synchronized void ensureCapacity(int minCapacity) {
        if (minCapacity > elementData.length) {
            int newCapacity = (capacityIncrement > 0) ? (elementData.length + capacityIncrement) : (elementData.length * 2);
            if (newCapacity < minCapacity) newCapacity = minCapacity;
            Object[] oldData = elementData;
            elementData = new Object[newCapacity];
            System.arraycopy(oldData, 0, elementData, 0, elementCount);
        }
    }
    public synchronized int size() { return elementCount; }
    public synchronized boolean isEmpty() { return elementCount == 0; }
    public Enumeration<E> elements() {
        return new Enumeration<E>() {
            int count = 0;
            public boolean hasMoreElements() { return count < elementCount; }
            @SuppressWarnings("unchecked")
            public E nextElement() {
                synchronized (Vector.this) {
                    if (count < elementCount) return (E) elementData[count++];
                }
                throw new NoSuchElementException("Vector Enumeration");
            }
        };
    }
    public synchronized boolean contains(Object o) { return indexOf(o, 0) >= 0; }
    public int indexOf(Object o) { return indexOf(o, 0); }
    public synchronized int indexOf(Object o, int index) {
        if (o == null) {
            for (int i = index ; i < elementCount ; i++)
                if (elementData[i]==null) return i;
        } else {
            for (int i = index ; i < elementCount ; i++)
                if (o.equals(elementData[i])) return i;
        }
        return -1;
    }
    public synchronized int lastIndexOf(Object o) { return lastIndexOf(o, elementCount-1); }
    public synchronized int lastIndexOf(Object o, int index) {
        if (index >= elementCount) throw new IndexOutOfBoundsException();
        if (o == null) {
            for (int i = index; i >= 0; i--)
                if (elementData[i]==null) return i;
        } else {
            for (int i = index; i >= 0; i--)
                if (o.equals(elementData[i])) return i;
        }
        return -1;
    }
    @SuppressWarnings("unchecked")
    public synchronized E elementAt(int index) {
        if (index >= elementCount) throw new ArrayIndexOutOfBoundsException(index + " >= " + elementCount);
        return (E) elementData[index];
    }
    @SuppressWarnings("unchecked")
    public synchronized E firstElement() {
        if (elementCount == 0) throw new NoSuchElementException();
        return (E) elementData[0];
    }
    @SuppressWarnings("unchecked")
    public synchronized E lastElement() {
        if (elementCount == 0) throw new NoSuchElementException();
        return (E) elementData[elementCount - 1];
    }
    public synchronized void setElementAt(E obj, int index) {
        if (index >= elementCount) throw new ArrayIndexOutOfBoundsException(index + " >= " + elementCount);
        elementData[index] = obj;
    }
    public synchronized void removeElementAt(int index) {
        if (index >= elementCount) throw new ArrayIndexOutOfBoundsException(index + " >= " + elementCount);
        else if (index < 0) throw new ArrayIndexOutOfBoundsException(index);
        int j = elementCount - index - 1;
        if (j > 0) System.arraycopy(elementData, index + 1, elementData, index, j);
        elementCount--;
        elementData[elementCount] = null;
    }
    public synchronized void insertElementAt(E obj, int index) {
        if (index > elementCount) throw new ArrayIndexOutOfBoundsException(index + " > " + elementCount);
        ensureCapacity(elementCount + 1);
        System.arraycopy(elementData, index, elementData, index + 1, elementCount - index);
        elementData[index] = obj;
        elementCount++;
    }
    public synchronized void addElement(E obj) {
        ensureCapacity(elementCount + 1);
        elementData[elementCount++] = obj;
    }
    public synchronized boolean removeElement(Object obj) {
        int i = indexOf(obj);
        if (i >= 0) {
            removeElementAt(i);
            return true;
        }
        return false;
    }
    public synchronized void removeAllElements() {
        for (int i = 0; i < elementCount; i++) elementData[i] = null;
        elementCount = 0;
    }
    @SuppressWarnings("unchecked")
    public synchronized E get(int index) {
        if (index >= elementCount) throw new ArrayIndexOutOfBoundsException(index);
        return (E) elementData[index];
    }
    @SuppressWarnings("unchecked")
    public synchronized E set(int index, E element) {
        if (index >= elementCount) throw new ArrayIndexOutOfBoundsException(index);
        E oldValue = (E) elementData[index];
        elementData[index] = element;
        return oldValue;
    }
    public synchronized boolean add(E e) {
        addElement(e);
        return true;
    }
    public boolean remove(Object o) { return removeElement(o); }
    public void add(int index, E element) { insertElementAt(element, index); }
    public synchronized E remove(int index) {
        E oldValue = get(index);
        removeElementAt(index);
        return oldValue;
    }
    public void clear() { removeAllElements(); }
}
