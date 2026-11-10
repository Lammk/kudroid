/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.animation;

import java.util.ArrayList;

final class AnimatorSetNode implements Cloneable {
    Animator mAnimation;

    /**
     * Child nodes are the nodes associated with animations that will be played immediately
     * after current node.
     */
    ArrayList<AnimatorSetNode> mChildNodes = null;

    /**
     * Flag indicating whether the animation in this node is finished. This flag
     * is used by AnimatorSet to check, as each animation ends, whether all child animations
     * are mEnded and it's time to send out an end event for the entire AnimatorSet.
     */
    boolean mEnded = false;

    /**
     * Nodes with animations that are defined to play simultaneously with the animation
     * associated with this current node.
     */
    ArrayList<AnimatorSetNode> mSiblings;

    /**
     * Parent nodes are the nodes with animations preceding current node's animation. Parent
     * nodes here are derived from user defined animation sequence.
     */
    ArrayList<AnimatorSetNode> mParents;

    /**
     * Latest parent is the parent node associated with a animation that finishes after all
     * the other parents' animations.
     */
    AnimatorSetNode mLatestParent = null;

    boolean mParentsAdded = false;
    long mStartTime = 0;
    long mEndTime = 0;
    long mTotalDuration = 0;

    /**
     * Constructs the AnimatorSetNode with the animation that it encapsulates. A AnimatorSetNode has no
     * dependencies by default; dependencies are added via the addDependency()
     * method.
     *
     * @param animation The animation that the AnimatorSetNode encapsulates.
     */
    public AnimatorSetNode(Animator animation) {
        this.mAnimation = animation;
    }

    @Override
    public AnimatorSetNode clone() {
        try {
            AnimatorSetNode node = (AnimatorSetNode) super.clone();
            node.mAnimation = mAnimation.clone();
            if (mChildNodes != null) {
                node.mChildNodes = new ArrayList<>(mChildNodes);
            }
            if (mSiblings != null) {
                node.mSiblings = new ArrayList<>(mSiblings);
            }
            if (mParents != null) {
                node.mParents = new ArrayList<>(mParents);
            }
            node.mEnded = false;
            return node;
        } catch (CloneNotSupportedException e) {
           throw new AssertionError();
        }
    }

    void addChild(AnimatorSetNode node) {
        if (mChildNodes == null) {
            mChildNodes = new ArrayList<>();
        }
        if (!mChildNodes.contains(node)) {
            mChildNodes.add(node);
            node.addParent(this);
        }
    }

    public void addSibling(AnimatorSetNode node) {
        if (mSiblings == null) {
            mSiblings = new ArrayList<AnimatorSetNode>();
        }
        if (!mSiblings.contains(node)) {
            mSiblings.add(node);
            node.addSibling(this);
        }
    }

    public void addParent(AnimatorSetNode node) {
        if (mParents == null) {
            mParents =  new ArrayList<AnimatorSetNode>();
        }
        if (!mParents.contains(node)) {
            mParents.add(node);
            node.addChild(this);
        }
    }

    public void addParents(ArrayList<AnimatorSetNode> parents) {
        if (parents == null) {
            return;
        }
        int size = parents.size();
        for (int i = 0; i < size; i++) {
            addParent(parents.get(i));
        }
    }
}
